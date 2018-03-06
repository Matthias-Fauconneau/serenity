#include "parse.h"
#include "matrix.h"
#include "window.h"
#include "image-render.h"
#include "mwc.h"
#include "algorithm.h"
#include "drag.h"

template<bool B, class T, class F> struct conditional { typedef T type; };
template<class T, class F> struct conditional<false, T, F> { typedef F type; };

generic static inline T rcp(T x) { return 1/x; }
generic static inline T select(bool c, T t, T f) { return c ? t : f; }
generic static inline T mix(const T& a, const T& b, const float t) { return (1-t)*a + t*b; }

generic static inline void rotateLeft(T& a, T& b, T& c) { T t = a; a = b; b = c; c = t; }
generic static inline void rotateRight(T& a, T& b, T& c) { T t = c; c = b; b = a; a = t; }

generic T hsum(const T& x) { return x; }
generic T mask(bool c, const T& t) { return c ? t : 0; }

template<Type Function, template<Type> Type V0, Type T0, uint N, template<Type> Type... V, Type... T> static inline constexpr
auto apply(Function function, vec<V,T,N>... sources) {
    vec<V0,T0,N> target;
    for(size_t index: range(N)) target[index] = function(sources[index]...);
    return target;
}

#define genericVecT1 template<template<Type> Type V, Type T, uint N, Type T1> static inline constexpr
genericVecT1 Vec mask(const vec<V,T1,N>& c, const Vec& t) { return apply(mask, c, t); }
genericVecT1 Vec select(const vec<V,T1,N>& c, const Vec& t, const Vec& f) { return apply(select, c, t, f); }
genericVecT1 auto hsum(const Vec& x) { return apply(hsum, x); }

template<Type T, uint N> struct VecT { T _[N]; };

template<template<Type> class V, uint N, Type T/*, decltype(T()[0]) = 0*/> auto transpose(const Vec& M) {
    vec<vec<V,Type remove_reference<decltype(T()[0])>::type,N>,T::N> Mt;
    for(uint i: range(N1)) for(uint j: range(N)) Mt[j][i] = M[i][j];
    return Mt;
}

//genericVec vec<x,Vec,1> transpose(const vec<V,vec<x,T,1>,N>& M) { return M; }

typedef VecT<vec3, 4> Quad;

static inline bool operator==(Quad A, Quad B) { return ref<vec3>(A._) == ref<vec3>(B._); }
template<> String str(const Quad& A) { return str(A._); }

static constexpr float ε = 0x1p-20;

static inline int allVerticesSameSidePlane(Quad A, Quad B) {
    const vec3 N = normalize(cross(B._[1]-B._[0], B._[3]-B._[0]));
    int sign = 0;
    for(vec3 v: A._) {
        const vec3 OP = v-B._[0];
        const float t = dot(N, OP);
        const float εOP = ε*length(OP); // equivalent to t=dot(N, normalize(OP)) without /0
        if(t > +εOP) {
            if(sign<0) return 0;
            sign = +1;
        }
        if(t < -εOP) {
            if(sign>0) return 0;
            sign = -1;
        }
    }
    return sign;
}

static inline int opCmp(Quad A, Quad B) {
    const vec3 minA = ::min<vec3>(A._), maxA = ::max<vec3>(A._);
    const vec3 minB = ::min<vec3>(B._), maxB = ::max<vec3>(B._);
    if(-minA.z < -maxB.z) return -1;
    if(-minB.z < -maxA.z) return +1;
    if(allVerticesSameSidePlane(A, B) > 0) return -1;
    if(allVerticesSameSidePlane(B, A) > 0) return +1;
    return 0; // ==
}

static inline bool operator <(Quad A, Quad B) { return opCmp(A, B) < 0; }

static inline bool intersect(const vec3 v0, const vec3 v1, const vec3 v2, const vec3 v3, const vec3 O, const vec3 D, vec3& N, float& nearestT, float& u, float& v) {
    const vec3 e01 = v1-v0;
    const vec3 e12 = v2-v1;
    const vec3 e23 = v3-v2;
    const vec3 e30 = v0-v3;
    N = cross(e01, e12);

    const float Nv0 = dot(N, v0-O);
    const vec3 e01v0O = _cross(e01,v0-O);
    const vec3 e12v2O = _cross(e12,v2-O);
    const vec3 e23v2O = _cross(e23,v2-O);
    const vec3 e30v0O = _cross(e30,v0-O);

    const float V0 = dot(e01v0O, D);
    const float U1 = dot(e12v2O, D);
    const float V1 = dot(e23v2O, D);
    const float U0 = dot(e30v0O, D);
    if(!(max(max(max(U0,V1),U1),V0) <= 0)) return false;
    const float det = dot(N, D);
    if(!(det < 0)) return false;
    const float rcpDet = rcp( det );
    const float t = Nv0 * rcpDet;
    if(!(t > 0)) return false;
    if(!(t < nearestT)) return false;
    nearestT = t;
    u = select(U0+V0 < 1, rcpDet * U0, 1 - (rcpDet * U1));
    v = select(U0+V0 < 1, rcpDet * V0, 1 - (rcpDet * V1));
    return true;
}

static inline bool intersect(const array<vec3>& vertices, const uint4& quad, const vec3 O, const vec3 D, vec3& N, float& nearestT, float& u, float& v) {
    return intersect(vertices[quad[0]], vertices[quad[1]], vertices[quad[2]], vertices[quad[3]], O, D, N, nearestT, u, v);
}

struct Scene {
    struct Quad {
        uint4 quad;
        Image3f outgoingRadiance; // for real surfaces: differential
        bool real = true;
        Image3f realOutgoingRadiance; // Faster rendering of synthetic real surfaces for synthetic test
        //const bgr3f albedo = 1;
    };

    array<vec3> vertices;
    array<Quad> quads;

    struct QuadLight { vec3 O, T, B, N; bgr3f emissiveFlux; };

    const float lightSize = 1;
    Scene::QuadLight light {{-lightSize/2,-lightSize/2,2}, {lightSize,0,0}, {0,lightSize,0}, {0,0,-sq(lightSize)}, 2/*lightSize/sq(lightSize)*/};
};

bool intersect(const Scene& scene, const Scene::Quad& quad, const vec3 O, const vec3 D, vec3& N, float& nearestT, float& u, float& v) {
    return intersect(scene.vertices, quad.quad, O, D, N, nearestT, u, v);
}

void importSTL(Scene& scene, string path, vec3 origin, bool real) {
    TextData s (readFile(path));
    s.skip("solid "); s.line(); // name
    array<uint3> triangles;
    array<vec3> vertices;
    while(!s.match("endsolid")) {
        s.whileAny(' ');
        s.skip("facet normal "); s.line(); // normal
        s.whileAny(' '); s.skip("outer loop\n");
        uint3 triangle;
        for(const uint triangleVertex: range(3)) {
            s.whileAny(' '); s.skip("vertex "); const vec3 v = parse<vec3>(s); s.skip('\n');
            const uint index = vertices.add(v);
            triangle[triangleVertex] = index;
        }
        triangles.append(triangle);
        s.whileAny(' '); s.skip("endloop\n");
        s.whileAny(' '); s.skip("endfacet\n");
    }
    s.line(); // name
    assert_(!s);
    assert_(vertices.size==8);

    { // Rescale
        const vec3 min = ::min(vertices);
        const vec3 max = ::max(vertices);
        for(vec3& v: vertices) v = (v-min)/(max-min)*2.f-vec3(1); // -> [-1, 1]
        for(vec3& v: vertices) v /= 4;
        for(vec3& v: vertices) v += origin;
    }

    const uint base = scene.vertices.size;
    while(triangles) {
        uint3 A = triangles.take(0);
        for(uint i: range(triangles.size)) {
            uint3 B = triangles[i];
            for(uint edgeIndex: range(3)) {
                const uint32 e0 = B[edgeIndex];
                const uint32 e1 = B[(edgeIndex+1)%2];
                /**/ if(A[1] == e0 && A[0] == e1) rotateLeft(A[0], A[1], A[2]);
                else if(A[2] == e0 && A[1] == e1) rotateRight(A[0], A[1], A[2]);
                else if(A[0] == e0 && A[2] == e1) {}
                else continue;
                const uint32 A3 = B[(edgeIndex+2)%3];
                const uint2 size = uint2(256); // FIXME: auto resolution
                scene.quads.append({uint4(base+A[0],base+A[1],base+A[2],base+A3), Image3f(size), real, real?Image3f(size):Image3f()});
            }
        }
    }

    scene.vertices.append(vertices);
}

void step(Scene& scene, Random& random) {
    Time time (true);
    for(const Scene::Quad& quad: scene.quads) {
        const vec3 v0 = scene.vertices[quad.quad[0]];
        const vec3 v1 = scene.vertices[quad.quad[1]];
        const vec3 v2 = scene.vertices[quad.quad[2]];
        const vec3 v3 = scene.vertices[quad.quad[3]];
        const vec3 N = normalize(cross(v1-v0, v3-v0));

        const uint2 size = quad.outgoingRadiance.size;
        for(uint y: range(size.y)) {
            const float v = float(y)/float(size.y-1);
            for(uint x: range(size.x)) {
                const float u = float(x)/float(size.x-1);
                const vec3 O = u+v<1 ? v0 + (v1-v0)*u + (v3-v0)*v : v2 + (v3-v2)*(1-u) + (v1-v2)*(1-v);
                static constexpr uint K = 1;
                typedef conditional<(K>1), float32 __attribute((ext_vector_type(K))), vec<::x, float32, 1>>::type vsf;
                /*typedef vec<bgr, v8sf, 3> bgr3fv8;
                typedef vec<xyz, v8sf, 3> vec3v8;*/
                typedef vec<bgr, vsf, 3> bgr3fv;
                typedef vec<xyz, vsf, 3> vec3v;
                bgr3fv differentialOutgoingRadianceSum = bgr3fv(0);
                bgr3fv realOutgoingRadianceSum = bgr3fv(0); // Synthetic test case
                const uint sampleCount = 8;
                for(uint unused i: range(sampleCount/8)) {
                    const Scene::QuadLight light = scene.light;
                    const vec3v L = vec3v(light.O) + random.next<vsf>() * vec3v(light.T) + random.next<vsf>() * vec3v(light.B);
                    const vec3v D = L - vec3v(O);

                    const vsf dotNL = max<vsf>(0, dot(vec3v(N), D));
                    //if all(dotNL <= 0) continue;
                    const vsf dotAreaL = max<vsf>(0, - dot(vec3v(light.N), D));
                    //if all(dotAreaL <= 0) continue;

                    bgr3fv incomingRadiance = bgr3fv(light.emissiveFlux) * dotNL * dotAreaL / sq(dot(D, D)); // Directional light

                    bool differential = quad.real;
                    vsf nearestRealT = inff, nearestVirtualT = inff;
                    bgr3fv realIncomingRadiance = incomingRadiance;
                    for(const Scene::Quad& quad: scene.quads) for(uint k: range(8)) { // FIXME: SIMD ray, SIMD setup
                        vec3 N; float u,v;
                        if(!differential || !quad.real) {
                            float t = nearestVirtualT[k];
                            intersect(scene, quad, O, transpose(D)[k], N, t/*nearestVirtualT*/, u, v);
                            nearestVirtualT[k] = t;
                        } else {
                            float t = nearestRealT[k];
                            if(intersect(scene, quad, O, transpose(D)[k], N, t/*nearestRealT*/, u, v)) {
                                realIncomingRadiance[k] = 0;
                            }
                            nearestRealT[k] = t;
                        }
                    }
                    const bgr3fv differentialIncomingRadiance = differential ? and(nearestVirtualT < nearestRealT, -incomingRadiance)
                                                                             : and(nearestVirtualT == vsf(inff), incomingRadiance);
                    const bgr3fv albedo = 1;
                    differentialOutgoingRadianceSum += albedo * differentialIncomingRadiance;
                    realOutgoingRadianceSum += albedo * realIncomingRadiance;
                }
                quad.outgoingRadiance(x, y) = (1/float(sampleCount)) * hsum(differentialOutgoingRadianceSum);
                if(quad.realOutgoingRadiance) quad.realOutgoingRadiance(x, y) = (1/float(sampleCount)) * hsum(realOutgoingRadianceSum);
            }
        }
    }
    log(time.milliseconds(),"ms");
}

static struct Test : Drag {
    Scene scene;

    Random random;

    vec3 viewPosition = vec3(0,0,0); //vec3(-1./2,0,0);

    unique<Window> window = ::window(this, 2048, mainThread, 0);

    Test() : Drag(vec2(0,-π/3)) {
        {
            const uint2 size = uint2(512); // FIXME: auto resolution
            scene.quads.append(Scene::Quad{uint4(
                                           scene.vertices.add(vec3(-1,-1,0)), scene.vertices.add(vec3(1,-1,0)),
                                           scene.vertices.add(vec3(1,1,0)), scene.vertices.add(vec3(-1,1,0))),
                                           Image3f(size), true, Image3f(size)});
        }

        importSTL(scene, "Cube.stl", vec3(-1./2, 0, +1./4+ε), true );
        importSTL(scene, "Cube.stl", vec3(+1./2, 0, +1./4+ε), false);
        importSTL(scene, "Cube.stl", vec3(0, -1./2, +1./4+ε), false);
        importSTL(scene, "Cube.stl", vec3(0, +1./2, +1./4+ε), false);

        step(scene, random);

        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;

        const float near = 3, far = near + 3; // FIXME: fit far
        const mat4 view = mat4().translate({0,0,-near-1}).rotateX(Drag::value.y).rotateZ(Drag::value.x);

        Time time {true};

        target.clear(byte4(0,0,0,0xFF));

        // Transform
        buffer<vec3> viewVertices (scene.vertices.size);
        for(const uint i: range(scene.vertices.size)) viewVertices[i] = view * scene.vertices[i];

        buffer<uint> quads (scene.quads.size, 0);
        for(const uint i: range(scene.quads.size)) {
            vec3 A[4]; for(const uint v: ::range(4)) A[v] = viewVertices[scene.quads[i].quad[v]];
            const vec3 N = cross(A[1]-A[0], A[3]-A[0]);
            if(dot(N, A[0]) >= 0) continue; // Back facing
            quads.append(i);
        }

        { // Z-Order polygon quick sort
            typedef uint T;
            const mref<T> at = quads;

            int2 stack[32];
            stack[0] = {0, int(at.size)-1};
            int top = 0;
            while(top >= 0) {
                const int2 range = stack[top];
                top--;

                const int left  = range[0];
                const int right = range[1];

                if(left < right) { // If the list has 2 or more items
                    swap(at[(left + right)/2], at[right]);
                    const T& pivot = at[right];
                    int pivotIndex = left;
                    for(const uint i: ::range(left,right)) { // Split
                        Quad A; { const uint4 a = scene.quads[at[i]].quad; for(const uint v: ::range(4)) A._[v] = viewVertices[a[v]]; }
                        Quad B; { const uint4 b = scene.quads[pivot].quad; for(const uint v: ::range(4)) B._[v] = viewVertices[b[v]]; }
                        if(A < B) {
                            swap(at[pivotIndex], at[i]);
                            pivotIndex++;
                        }
                    }
                    swap(at[pivotIndex], at[right]);
                    // Push larger partition first to start with smaller partition and guarantee stack < log2 N
                    if(pivotIndex-left > right-pivotIndex) {
                        top++;
                        stack[top] = {left, pivotIndex-1};
                        top++;
                        stack[top] = {pivotIndex+1, right};
                    } else {
                        top++;
                        stack[top] = {pivotIndex+1, right};
                        top++;
                        stack[top] = {left, pivotIndex-1};
                    }
                }
            }
        }

        const mat4 projection = perspective(near, far); // .scale(vec3(1, W/H, 1))
        static constexpr int32 pixel = 16; // 11.4
        const mat4 NDC = mat4()
                .scale(vec3(vec2(target.size*uint(pixel/2u)), 1<<13)) // 0, 2 -> 11.4, .14
                .translate(vec3(1)); // -1, 1 -> 0, 2
        const mat4 M = NDC * projection;

        buffer<uint64> coverageBuffer (target.ref::size/64); // Bitmask of covered samples
        coverageBuffer.clear(0);

        for(const uint quadIndex: quads) { // Z-sorted
            Scene::Quad& quad = scene.quads[quadIndex];

            int2 V[4];
            float iw[4]; // FIXME: float
            for(const uint i: range(4)) {
                vec3 xyw = (M * vec4(viewVertices[quad.quad[i]], 1)).xyw();
                iw[i] = 1 / xyw[2]; // FIXME: float
                V[i] = int2(iw[i] * xyw.xy());
            }

            const int2 min = ::min<int2>(V);
            const int2 max = ::max<int2>(V);

            // Setup
            int e01x = V[0].y - V[1].y, e01y = V[1].x - V[0].x, e01z = V[0].x * V[1].y - V[1].x * V[0].y;
            int e12x = V[1].y - V[2].y, e12y = V[2].x - V[1].x, e12z = V[1].x * V[2].y - V[2].x * V[1].y;
            int e23x = V[2].y - V[3].y, e23y = V[3].x - V[2].x, e23z = V[2].x * V[3].y - V[3].x * V[2].y;
            int e30x = V[3].y - V[0].y, e30y = V[0].x - V[3].x, e30z = V[3].x * V[0].y - V[0].x * V[3].y;

            typedef float T;
            typedef vec3 vecN;
            const vecN A0 = vecN(0, 0, 1);
            const vecN A1 = vecN(1, 0, 1);
            const vecN A2 = vecN(1, 1, 1);
            const vecN A3 = vecN(0, 1, 1);

            const vecN a0 = iw[0] * A0;
            const vecN a1 = iw[1] * A1;
            const vecN a2 = iw[2] * A2;
            const vecN a3 = iw[3] * A3;

            const int e13x = V[1].y - V[3].y, e13y = V[3].x - V[1].x, e13z = V[1].x * V[3].y - V[3].x * V[1].y;

            const vecN e013x = T(e01x)*a3 + T(e13x)*a0 + T(e30x)*a1;
            const vecN e013y = T(e01y)*a3 + T(e13y)*a0 + T(e30y)*a1;
            const vecN e013z = T(e01z)*a3 + T(e13z)*a0 + T(e30z)*a1;

            const vecN e123x = T(e12x)*a3 + T(e23x)*a1 - T(e13x)*a2;
            const vecN e123y = T(e12y)*a3 + T(e23y)*a1 - T(e13y)*a2;
            const vecN e123z = T(e12z)*a3 + T(e23z)*a1 - T(e13z)*a2;

            e01z += (e01x>0/*dy<0*/ || (e01x==0/*dy=0*/ && e01y<0/*dx<0*/));
            e12z += (e12x>0/*dy<0*/ || (e12x==0/*dy=0*/ && e12y<0/*dx<0*/));
            e23z += (e23x>0/*dy<0*/ || (e23x==0/*dy=0*/ && e23y<0/*dx<0*/));
            e30z += (e30x>0/*dy<0*/ || (e30x==0/*dy=0*/ && e30y<0/*dx<0*/));

            for(const uint targetY: range(::max(0, min.y/pixel), ::min<uint>(target.size.y-1, max.y/pixel+1))) {
                const uint Y = pixel*targetY + pixel/2;
                for(const uint targetX: range(::max(0, min.x/pixel), ::min<uint>(target.size.x-1, max.x/pixel+1))) {
                    const uint targetI = targetY*target.size.x+targetX;
                    if(coverageBuffer[targetI/64]&(1ull<<(targetI%64))) continue;

                    const uint X = pixel*targetX + pixel/2;

                    const int step01 = e01z + e01x*X + e01y*Y;
                    const int step12 = e12z + e12x*X + e12y*Y;
                    const int step23 = e23z + e23x*X + e23y*Y;
                    const int step30 = e30z + e30x*X + e30y*Y;

                    if(step01>0 && step12>0 && step23>0 && step30>0) {
                        const int step13 = e13z + e13x*X + e13y*Y;
                        const vecN a = step13>0 ? vecN(e013z + e013x*T(X) + e013y*T(Y)) :
                                                  vecN(e123z + e123x*T(X) + e123y*T(Y)) ;
                        const vecN A = a / a[2]; // FIXME: float
                        const float u = A.x;
                        const float v = A.y;

                        // FIXME: fold texSize in UV vertex attributes
                        const uint2 texSize = quad.outgoingRadiance.size;
                        const uint texX = u*(texSize.x-1)+1.f/2;
                        const uint texY = v*(texSize.y-1)+1.f/2;

                        const bgr3f realOutgoingRadiance = quad.real ? quad.realOutgoingRadiance(texX, texY) : 0; // FIXME: synthetic test case
                        const bgr3f differentialOutgoingRadiance = quad.outgoingRadiance(texX, texY); // FIXME: bilinear
                        const bgr3f finalOutgoingRadiance = realOutgoingRadiance + differentialOutgoingRadiance;
                        target(targetX, target.size.y-1-targetY) = byte4(sRGB(finalOutgoingRadiance.b), sRGB(finalOutgoingRadiance.g), sRGB(finalOutgoingRadiance.r), 0xFF);

                        coverageBuffer[targetI/64] |= (1ull<<targetI%64);
                    }
                }
            }
        }
        if(time.milliseconds()>100) log(time.milliseconds(),"ms");
        //value.x = __builtin_fmod(value.x + π*(3-sqrt(5.)), 2*π);
        //value.y = __builtin_fmod(value.y + π*(3-sqrt(5.)), 2*π);
        //window->render();
    }
    virtual vec2 drag(vec2 dragStartValue, vec2 normalizedDragOffset) override {
        vec2 value = dragStartValue + float(2*π)*normalizedDragOffset;
        value.y = clamp<float>(-π/2, value.y, 0);
        return value;
    }
} test;
