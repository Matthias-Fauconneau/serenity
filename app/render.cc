#include "parse.h"
#include "matrix.h"
#include "window.h"
#include "image-render.h"
#include "mwc.h"
#include "algorithm.h"
#include "drag.h"

template<> inline vec<x,float,1> Random::next<vec<x,float,1>>() { return vec<x,float,1>(next<float>()); }

template<bool B, Type T, Type F> struct conditional { typedef T type; };
template<Type T, Type F> struct conditional<false, T, F> { typedef F type; };

generic static inline T select(bool c, T t, T f) { return c ? t : f; }
generic static inline T mix(const T& a, const T& b, const float t) { return (1-t)*a + t*b; }

generic static inline void rotateLeft(T& a, T& b, T& c) { T t = a; a = b; b = c; c = t; }
generic static inline void rotateRight(T& a, T& b, T& c) { T t = c; c = b; b = a; a = t; }

//genericVec T hsum<Vec>(const Vec& t) { return hsum<V,T,N>(t); } function template partial specialization is not allowed
template<> float hsum<vec<x,float,1>>(vec<x,float,1> t) { return hsum<x,float,1>(t); }

generic T mask(bool c, T t) { return c ? t : T(); }

template<template<Type> Type V, uint N, Type Function, Type... T> static inline constexpr
auto apply(Function function, vec<V,T,N>... sources) {
    vec<V,decltype(function(sources[0]...)),N> target;
    for(size_t index: range(N)) target[index] = function(sources[index]...);
    return target;
}

#define genericVecT1 template<template<Type> Type V, Type T, uint N, Type T1> static inline constexpr

genericVecT1 Vec mask(const vec<V,T1,N>& c, const Vec& t) { return apply(mask<T>, c, t); }
genericVecT1 Vec select(const vec<V,T1,N>& c, const Vec& t, const Vec& f) { return apply(select, c, t, f); }

template<template<Type> Type V, Type T, uint N, Type T1, decltype(sizeof(T1)/T1()[0]==N) = 0> static inline constexpr
 Vec mask(const T1& c, const Vec& t) { return apply([c](const T& t){return ::mask(c, t);}, t); }

template<template<Type> Type V, uint N, Type T> auto getᵀ(const vec<V,T,N>& M, const uint i) {
    vec<V,Type remove_reference<decltype(T()[0])>::type, N> v;
    for(uint j: range(N)) v[j] = M[j][i];
    return v;
}

template<template<Type> Type V, uint N, Type T> void setᵀ(vec<V,T,N>& M, uint i, const vec<V,Type remove_reference<decltype(T()[0])>::type, N>& v) {
    for(uint j: range(N)) M[j][i] = v[j];
}

template<Type T, uint N> struct VecT {
    T _[N];
    T& operator[](size_t i) { return _[i]; }
};

typedef VecT<vec3, 4> Quad;

template<> String str(const Quad& A) { return str(A._); }

static constexpr float ε = 0x1p-20;

static inline int allVerticesSameSidePlane(Quad A, Quad B) {
    const vec3 N = normalize(cross(B[1]-B[0], B[3]-B[0]));
    int sign = 0;
    for(vec3 v: A._) {
        const vec3 OP = v-B[0];
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

static constexpr uint K = 8;
typedef conditional<(K>1), float32 __attribute((ext_vector_type(K))),
                           vec<::x, float32, 1>                       >::type floatv;
typedef conditional<(K>1), int32 __attribute((ext_vector_type(K))),
                           vec<::x, bool, 1>                       >::type boolv;
typedef vec<bgr, floatv, 3> bgr3fv;
typedef vec<xyz, floatv, 3> vec3v;

static inline boolv intersect(const vec3 v0, const vec3 v1, const vec3 v2, const vec3 v3, const vec3 O,
                              const vec3v D, vec3& N, floatv& nearestT, floatv& u, floatv& v) {
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

    const floatv V0 = dot(vec3v(e01v0O), D);
    const floatv U1 = dot(vec3v(e12v2O), D);
    const floatv V1 = dot(vec3v(e23v2O), D);
    const floatv U0 = dot(vec3v(e30v0O), D);
    //if(!(max(max(max(U0,V1),U1),V0) <= 0)) return false;
    const floatv det = dot(vec3v(N), D);
    //if(!(det < 0)) return false;
    const floatv det¯¹ = rcp( det );
    const floatv t = Nv0 * det¯¹;
    //if(!(t > 0)) return false;
    //if(!(t < nearestT)) return false;
    const boolv test = (max(max(max(U0,V1),U1),V0) <= floatv(0)) & (det < floatv(0)) & (t > floatv(0)) & (t < nearestT);
    nearestT = select(test, t, nearestT);
    u = select(test, select(U0+V0 < 1, det¯¹ * U0, 1 - (det¯¹ * U1)), u);
    v = select(test, select(U0+V0 < 1, det¯¹ * V0, 1 - (det¯¹ * V1)), v);
    return test;
}

static inline boolv intersect(const array<vec3>& vertices, const uint4& quad, const vec3 O, const vec3v D, vec3& N, floatv& nearestT, floatv& u, floatv& v) {
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
    Scene::QuadLight light {{-lightSize/2,-lightSize/2,2}, {lightSize,0,0}, {0,lightSize,0}, {0,0,-sq(lightSize)}, bgr3f(2)/*lightSize/sq(lightSize)*/};
};

static inline boolv intersect(const Scene& scene, const Scene::Quad& quad, const vec3 O, const vec3v D, vec3& N, floatv& nearestT, floatv& u, floatv& v) {
    return intersect(scene.vertices, quad.quad, O, D, N, nearestT, u, v);
}

static inline void importSTL(Scene& scene, string path, vec3 origin, bool real) {
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
    assert_(vertices.size==8, vertices.size, vertices);

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
                const uint2 size = uint2(2/*256*/); // FIXME: auto resolution
                scene.quads.append({uint4(base+A[0],base+A[1],base+A[2],base+A3), Image3f(size), real, real?Image3f(size):Image3f()});
            }
        }
    }

    scene.vertices.append(vertices);
}

static inline void step(Scene& scene, Random& random) {
    Time time (true);
    for(const Scene::Quad& quad: scene.quads) {
        const vec3 v0 = scene.vertices[quad.quad[0]];
        const vec3 v1 = scene.vertices[quad.quad[1]];
        const vec3 v2 = scene.vertices[quad.quad[2]];
        const vec3 v3 = scene.vertices[quad.quad[3]];
        const vec3 N = normalize(cross(v1-v0, v3-v0));

        const uint2 size = quad.outgoingRadiance.size;
        for(uint y: range(size.y)) {
            //const float v = (float(y)+0.5f)/float(size.y); // Uniform
            const float v = float(y)/float(size.y-1); // Duplicate edges
            for(uint x: range(size.x)) {
                //const float u = (float(x)+0.5f)/float(size.x); // Uniform
                const float u = float(x)/float(size.x-1); // Duplicate edges
                const vec3 O = u+v<1 ? v0 + (v1-v0)*u + (v3-v0)*v : v2 + (v3-v2)*(1-u) + (v1-v2)*(1-v);

                bgr3fv differentialOutgoingRadianceSum = bgr3fv(floatv(0.f));
                bgr3fv realOutgoingRadianceSum = bgr3fv(floatv(0.f)); // Synthetic test case
                const uint sampleCount = 8;
                for(unused uint i: range(sampleCount/K)) {
                    const Scene::QuadLight light = scene.light;
                    const vec3v L = vec3v(light.O) + random.next<floatv>() * vec3v(light.T) + random.next<floatv>() * vec3v(light.B);
                    const vec3v D = L - vec3v(O);

                    const floatv dotNL = max(floatv(0), dot(vec3v(N), D));
                    //if all(dotNL <= 0) continue;
                    const floatv dotAreaL = max(floatv(0), - dot(vec3v(light.N), D));
                    //if all(dotAreaL <= 0) continue;

                    bgr3fv incomingRadiance = bgr3fv(light.emissiveFlux) * dotNL * dotAreaL / sq(dot(D, D)); // Directional light

                    bool differential = quad.real;
                    floatv nearestRealT (inff), nearestVirtualT (inff);
                    bgr3fv realIncomingRadiance = incomingRadiance;
                    for(const Scene::Quad& quad: scene.quads) { // FIXME: SIMD ray, SIMD setup
                        vec3 N; floatv u,v;
                        if(!differential || !quad.real) intersect(scene, quad, O, D, N, nearestVirtualT, u, v);
                        else realIncomingRadiance = mask(~intersect(scene, quad, O, D, N, nearestRealT, u, v), realIncomingRadiance);
                    }
                    const bgr3fv differentialIncomingRadiance = differential ? mask(vecLt(nearestVirtualT,/* <*/ nearestRealT), -incomingRadiance)
                                                                             : mask(vecEq(nearestVirtualT,/*==*/ floatv(inff)), incomingRadiance);
                    const bgr3fv albedo = bgr3fv(floatv(1));
                    differentialOutgoingRadianceSum += albedo * differentialIncomingRadiance;
                    realOutgoingRadianceSum += albedo * realIncomingRadiance;
                }
                quad.outgoingRadiance(x, y) = bgr3f(1/float(sampleCount)) * apply(hsum<floatv>, differentialOutgoingRadianceSum);
                if(quad.realOutgoingRadiance) quad.realOutgoingRadiance(x, y) = bgr3f(1/float(sampleCount)) * apply(hsum<floatv>, realOutgoingRadianceSum);
            }
        }
    }
    log(time.milliseconds(),"ms");
}

template<Type V> V parseMat(TextData& s) {
    V A;
    s.whileAny(" \n");
    for(uint i: range(V::M)) {
        for(uint j: range(V::N)) {
            A(i,j) = parse<Type V::T>(s);
            if(j<V::N-1) s.whileAny(" \t");
        }
        s.whileAny(" \n");
    }
    return A;
}
template<> inline mat4 parse<mat4>(TextData& s) { return parseMat<mat4>(s); }

struct Render : Drag {
    Scene scene;

    Random random;

    vec3 viewPosition = vec3(0,0,0); //vec3(-1./2,0,0);

    unique<Window> window = ::window(this, int2(3840,2160), mainThread, 0);

    Render() : Drag(vec2(0,-π/3)) {
        {
            const uint2 size = uint2(128/*512*/); // FIXME: auto resolution
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

#if 0
        const float near = 3, far = near + 3; // FIXME: fit far
        const mat4 view = mat4().translate({0,0,-near-1}).rotateX(Drag::value.y).rotateZ(Drag::value.x);
        log("view\n"+str(view));
#else
        // R R R tx
        // R R R ty
        // R R R tz
        // 0 0 0 1 // FIXME
        const mat4 view = parse<mat4>("\
                                      0.073	   0.3518	  -0.0395	   0.0057 \
                                     0.2872	  -0.0203	   0.0565	   0.1354 \
                                    -0.2926	   0.0614	   0.9294	  -1.1163 \
                                     0.0359	  -0.0075	  -0.1142	   0.2018 ");
#endif

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
                        Quad A; { const uint4 a = scene.quads[at[i]].quad; for(const uint v: ::range(4)) A[v] = viewVertices[a[v]]; }
                        Quad B; { const uint4 b = scene.quads[pivot].quad; for(const uint v: ::range(4)) B[v] = viewVertices[b[v]]; }
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

#if 0
        const mat4 projection = perspective(near, far, float(target.size.x)/float(target.size.y)); // .scale(vec3(1, W/H, 1))
        log("projection\n"+str(projection));
#else
        /*const mat4 projection = parse<mat4>("\
                                            1.4881	    0	        0	        0 \
                                            0	   1.4881	        0	        0 \
                                            0	        0	        0	        0 \
                                            0	        0	        0	        1 ");*/
        const float focalLength = 4.2, pixelPitch = 0.0014;
        const float near = 2/(target.size.y*pixelPitch/focalLength);
        const float far = 1000/focalLength*near; //mm
        //const mat4 projection = perspective(near, far, float(target.size.x)/float(target.size.y)); // .scale(vec3(1, W/H, 1))
        const mat4 projection = perspective(near, far).scale(vec3(float(target.size.y)/float(target.size.x), 1, 1));
        log(projection);
#endif
#if 1
        {
            const float y = 297./210;
            const ref<vec2> modelC = {{-1,-y},{1,-y},{1,y},{-1,y}}; // FIXME: normalize origin and average distance ~ √2


            const mat4 NDC = mat4()
                    .scale(vec3(vec2(target.size)/2.f, 1))
                    .translate(vec3(1)); // -1, 1 -> 0, 2
            const mat4 flipY = mat4().translate(vec3(0, target.size.y-1, 0)).scale(vec3(1, -1, 1)); // Flips Y axis from Y bottom up to Y top down for ::line
            {
                const mat4 P = projection*view;
                const mat4 M = flipY*NDC*P;

                line(target, (M*vec3(modelC[0], 0)).xy(), (M*vec3(modelC[1], 0)).xy(), bgr3f(1));
                line(target, (M*vec3(modelC[1], 0)).xy(), (M*vec3(modelC[2], 0)).xy(), bgr3f(1));
                line(target, (M*vec3(modelC[2], 0)).xy(), (M*vec3(modelC[3], 0)).xy(), bgr3f(1));
                line(target, (M*vec3(modelC[3], 0)).xy(), (M*vec3(modelC[0], 0)).xy(), bgr3f(1));

                const float z = 0.1;
                line(target, (M*vec3(modelC[0], 0)).xy(), (M*vec3(modelC[0], z)).xy(), bgr3f(1));
                line(target, (M*vec3(modelC[1], 0)).xy(), (M*vec3(modelC[1], z)).xy(), bgr3f(1));
                line(target, (M*vec3(modelC[2], 0)).xy(), (M*vec3(modelC[2], z)).xy(), bgr3f(1));
                line(target, (M*vec3(modelC[3], 0)).xy(), (M*vec3(modelC[3], z)).xy(), bgr3f(1));

                line(target, (M*vec3(modelC[0], z)).xy(), (M*vec3(modelC[1], z)).xy(), bgr3f(1));
                line(target, (M*vec3(modelC[1], z)).xy(), (M*vec3(modelC[2], z)).xy(), bgr3f(1));
                line(target, (M*vec3(modelC[2], z)).xy(), (M*vec3(modelC[3], z)).xy(), bgr3f(1));
                line(target, (M*vec3(modelC[3], z)).xy(), (M*vec3(modelC[0], z)).xy(), bgr3f(1));
            }

            const mat4 M = flipY*NDC*projection;
            for(const uint quadIndex: quads) { // Z-sorted
                Scene::Quad& quad = scene.quads[quadIndex];
                vec3 V[4];
                for(const uint i: range(4)) V[i] = M * viewVertices[quad.quad[i]];
                for(const uint i: range(4)) line(target, V[i].xy(), V[(i+1)%4].xy(), bgr3f(1));
            }
        }
#endif
#if 1
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

                        const bgr3f realOutgoingRadiance = quad.real ? quad.realOutgoingRadiance(texX, texY) : bgr3f(0); // FIXME: synthetic test case
                        const bgr3f differentialOutgoingRadiance = quad.outgoingRadiance(texX, texY); // FIXME: bilinear
                        const bgr3f finalOutgoingRadiance = realOutgoingRadiance + differentialOutgoingRadiance;
                        target(targetX, target.size.y-1-targetY) = byte4(sRGB(finalOutgoingRadiance), 0xFF);

                        coverageBuffer[targetI/64] |= (1ull<<targetI%64);
                    }
                }
            }
        }
        if(time.milliseconds()>100) log(time.milliseconds(),"ms");
        //value.x = __builtin_fmod(value.x + π*(3-sqrt(5.)), 2*π);
        //value.y = __builtin_fmod(value.y + π*(3-sqrt(5.)), 2*π);
        //window->render();
#endif
    }
    virtual vec2 drag(vec2 dragStartValue, vec2 normalizedDragOffset) override {
        vec2 value = dragStartValue + float(2*π)*normalizedDragOffset;
        value.y = clamp<float>(-π/2, value.y, 0);
        return value;
    }
} static render;
