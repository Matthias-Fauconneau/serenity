#include "parse.h"
#include "matrix.h"
#include "window.h"
#include "image-render.h"
#include "mwc.h"
#include "algorithm.h"

generic T mix(const T& a, const T& b, const float t) { return (1-t)*a + t*b; }

generic inline void rotateLeft(T& a, T& b, T& c) { T t = a; a = b; b = c; c = t; }
generic inline void rotateRight(T& a, T& b, T& c) { T t = c; c = b; b = a; a = t; }

struct Scene {
    struct Quad {
        uint4 quad;
        Image3f outgoingRadiance; // for real surfaces: differential
        bool real = true;
        //Image3f realOutgoingRadiance; // Faster rendering of synthetic real surfaces for synthetic test
        //const bgr3f albedo = 1;
    };

    array<vec3> vertices;
    array<Quad> quads;

    struct QuadLight { vec3 O, T, B, N; bgr3f emissiveFlux; };

    const float lightSize = 1;
    Scene::QuadLight light {{-lightSize/2,-lightSize/2,2}, {lightSize,0,0}, {0,lightSize,0}, {0,0,-sq(lightSize)}, 2/*lightSize/sq(lightSize)*/};
};

template<> String str(const Scene::Quad& q) { return str(q.quad); }

generic T select(bool mask, T t, T f) { return mask ? t : f; }
generic T rcp(T x) { return 1/x; }

bool intersect(const vec3 vA, const vec3 vB, const vec3 vC, const vec3 vD, const vec3 O, const vec3 D, vec3& N, float& nearestT, float& u, float& v) {
    const vec3 eAB = vB-vA;
    const vec3 eBC = vC-vB;
    const vec3 eCD = vD-vC;
    const vec3 eDA = vA-vD;
    N = cross(eAB, eBC);

    const float Nv0 = dot(N, vA-O);
    const vec3 eABvAO = _cross(eAB,vA-O);
    const vec3 eBCvCO = _cross(eBC,vC-O);
    const vec3 eCDvCO = _cross(eCD,vC-O);
    const vec3 eDAcAO = _cross(eDA,vA-O);

    const float V0 = dot(eABvAO, D);
    const float U1 = dot(eBCvCO, D);
    const float V1 = dot(eCDvCO, D);
    const float U0 = dot(eDAcAO, D);
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

bool intersect(const array<vec3>& vertices, const uint4& quad, const vec3 O, const vec3 D, vec3& N, float& nearestT, float& u, float& v) {
    return intersect(vertices[quad[0]], vertices[quad[1]], vertices[quad[2]], vertices[quad[3]], O, D, N, nearestT, u, v);
}
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
                scene.quads.append({uint4(base+A[0],base+A[1],base+A[2],base+A3), Image3f(256), real/*, real?Image3f(4):Image3f()*/});
            }
        }
    }

    scene.vertices.append(vertices);
}

void step(Scene& scene) {
    Time time (true);
    for(const Scene::Quad& quad: scene.quads) {
        const vec3 v0 = scene.vertices[quad.quad[0]];
        const vec3 v1 = scene.vertices[quad.quad[1]];
        const vec3 v2 = scene.vertices[quad.quad[2]];
        const vec3 v3 = scene.vertices[quad.quad[3]];
        const vec3 N = normalize(cross(v1-v0, v3-v0));

        const Image3f& target = quad.outgoingRadiance;
        for(uint y: range(target.size.y)) {
            const float v = float(y)/float(target.size.y-1);
            for(uint x: range(target.size.x)) {
                const float u = float(x)/float(target.size.x-1);
                //if(u+v < 1)
                //const vec3 O = v0*(1-v)*(1-u) + v1*(1-v)*u + v2*v*u + v3*v*(1-u);
                const vec3 O = u+v<1 ? v0 + (v1-v0)*u + (v3-v0)*v : v2 + (v3-v2)*(1-u) + (v1-v2)*(1-v);
                bgr3f differentialOutgoingRadianceSum = 0; // Directional light
                const uint sampleCount = 1;
                for(uint unused i: range(sampleCount)) {
                    bgr3f differentialIncomingRadiance;
                    {
                        v8sf random8 = random();
                        const vec2 uv (random8[0], random8[1]);
                        const Scene::QuadLight light = scene.light;
                        const vec3 D = (light.O + uv[0] * light.T + uv[1] * light.B) - O;

                        const float dotNL = dot(D, N);
                        if(dotNL <= 0) continue;
                        const float dotAreaL = - dot(light.N, D);
                        if(dotAreaL <= 0) continue;

                        bgr3f incomingRadiance = dotNL * dotAreaL / sq(sq(D)) * light.emissiveFlux; // Directional light

                        if(quad.real) {
                            float nearestRealT = inff, nearestVirtualT = inff;
                            for(const Scene::Quad& quad: scene.quads) {
                                vec3 N; float u,v;
                                if(!quad.real) intersect(scene, quad, O, D, N, nearestVirtualT, u, v);
                                if( quad.real) intersect(scene, quad, O, D, N, nearestRealT, u, v);
                            }
                            differentialIncomingRadiance = nearestVirtualT < nearestRealT ? - incomingRadiance : 0;
                        } else {
                            differentialIncomingRadiance = incomingRadiance;
                            if(0) for(const Scene::Quad& quad: scene.quads) {
                                float t = inff;
                                vec3 N; float u,v;
                                if(intersect(scene, quad, O, D, N, t, u, v)) { differentialIncomingRadiance = 0; break; };
                            }
                        }
                    }
                    const bgr3f albedo = 1;
                    differentialOutgoingRadianceSum += albedo * differentialIncomingRadiance;
                }
                target(x, y) = (1/float(sampleCount)) * differentialOutgoingRadianceSum;
                //target(x, y) = bgr3f(O.z, O.y, O.x);
            }
        }
    }
    log(time);
}

static struct Test : Widget {
    Scene scene;

    Random random;

    vec3 viewPosition = vec3(0,0,0); //vec3(-1./2,0,0);

    unique<Window> window = ::window(this, 2048, mainThread, 0);

    Test() {
        if(1) scene.quads.append(Scene::Quad{uint4(
                                       scene.vertices.add(vec3(-1,-1,0)), scene.vertices.add(vec3(1,-1,0)),
                                       scene.vertices.add(vec3(1,1,0)), scene.vertices.add(vec3(-1,1,0))),
                                       Image3f(512), true/*, Image3f(4)*/});

        importSTL(scene, "Cube.stl", vec3(-1./2, 0, +1./4), true );
        importSTL(scene, "Cube.stl", vec3(+1./2, 0, +1./4), false);

        // FIXME: front to back, coverage buffer

        step(scene);

        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;

        const float near = 3, far = near + 3; // FIXME: fit far
        const mat4 view = mat4().translate({0,0,-near-1}).rotateX(-π/4);

        Time time {true};

        target.clear(byte4(0,0,0,0xFF));

        const mat4 projection = perspective(near, far); // .scale(vec3(1, W/H, 1))
        static constexpr int32 pixel = 16; // 11.4
        const mat4 NDC = mat4()
                .scale(vec3(vec2(target.size*uint(pixel/2u)), 1<<13)) // 0, 2 -> 11.4, .14
                .translate(vec3(1)); // -1, 1 -> 0, 2
        const mat4 M = NDC * projection * view;

        for(const Scene::Quad& quad: scene.quads) { // TODO: Z-Order
            int2 V[4];
            float iw[4]; // FIXME: float
            for(const uint i: range(4)) {
                vec3 xyw = (M * vec4(scene.vertices[quad.quad[i]], 1)).xyw();
                iw[i] = 1 / xyw[2]; // FIXME: float
                V[i] = int2(iw[i] * xyw.xy());
            }

            const int2 min = ::min<int2>(V);
            const int2 max = ::max<int2>(V);

            for(const uint y: range(::max(0, min.y/pixel), ::min<uint>(target.size.y-1, max.y/pixel+1))) {
                const uint Y = pixel*y + pixel/2;
                for(const uint x: range(::max(0, min.x/pixel), ::min<uint>(target.size.x-1, max.x/pixel+1))) {
                    const uint X = pixel*x + pixel/2;

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

                        bgr3f realOutgoingRadiance; // FIXME: synthetic
                        if(!quad.real || 1) realOutgoingRadiance = bgr3f(0, 0, 0);
                        else { // Synthetic real
                            const uint sampleCount = 1;
                            bgr3f outgoingRadianceSum = 0;
                            for(uint unused i: range(sampleCount)) {
                                bgr3f realIncomingRadiance;
                                {
                                    const vec3 v0 = scene.vertices[quad.quad[0]];
                                    const vec3 v1 = scene.vertices[quad.quad[1]];
                                    const vec3 v3 = scene.vertices[quad.quad[3]];
                                    const vec3 N = normalize(cross(v1-v0, v3-v0));
                                    const vec3 O = v0 + (v1-v0) * u + (v3-v0) * v;

                                    v8sf random8 = random();
                                    const vec2 uv (random8[0], random8[1]);
                                    const Scene::QuadLight light = scene.light;
                                    const vec3 D = (light.O + uv[0] * light.T + uv[1] * light.B) - O;

                                    const float dotNL = dot(N, D);
                                    if(dotNL <= 0) continue;
                                    const float dotAreaL = - dot(light.N, D);
                                    if(dotAreaL <= 0) continue;

                                    realIncomingRadiance = dotNL * dotAreaL / sq(sq(D)) * light.emissiveFlux; // Directional light

                                    float nearestRealT = inff;
                                    for(const Scene::Quad& quad: scene.quads) {
                                        float u,v; vec3 N;
                                        if( quad.real) {
                                            if(intersect(scene, quad, O, D, N, nearestRealT, u, v)) realIncomingRadiance = 0;
                                        }
                                    }
                                }
                                const bgr3f albedo = 1;
                                outgoingRadianceSum += albedo * realIncomingRadiance;
                            }
                            const bgr3f outgoingRadiance = (1/float(sampleCount)) * outgoingRadianceSum;
                            realOutgoingRadiance = outgoingRadiance;
                        }

                        bgr3f differentialOutgoingRadiance; // or virtual outgoing radiance
                        {
                            // FIXME: fold texSize in UV vertex attributes
                            const uint2 texSize = quad.outgoingRadiance.size;
                            const uint x = u*(texSize.x-1);
                            const uint y = v*(texSize.y-1);
                            assert_(x<texSize.x && y<texSize.y, x, y, u, v);
                            differentialOutgoingRadiance = quad.outgoingRadiance(x, y); // FIXME: bilinear
                        }

                        const bgr3f finalOutgoingRadiance = realOutgoingRadiance + differentialOutgoingRadiance;
                        target(x, target.size.y-1-y) = byte4(sRGB(finalOutgoingRadiance.b), sRGB(finalOutgoingRadiance.g), sRGB(finalOutgoingRadiance.r), 0xFF);
                    }
                }
            }
        }
        log(time);
        //viewPosition += vec3(1./30,0,0);
        //window->render();
    }
} test;
