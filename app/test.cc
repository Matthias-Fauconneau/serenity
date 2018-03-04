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
        Image3f realOutgoingRadiance; // Faster rendering of synthetic real surfaces
        bgr3f albedo = 1;
        bool real = true;
    };

    array<vec3> vertices;
    array<Quad> quads;

    struct QuadLight { vec3 O, T, B, N; bgr3f emissiveFlux; };

    const float lightSize = 4;
    Scene::QuadLight light {{-lightSize/2,-lightSize/2,2}, {lightSize,0,0}, {0,lightSize,0}, {0,0,-sq(lightSize)}, 1/*lightSize/sq(lightSize)*/};
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

void importSTL(Scene& scene, string path) {
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
        for(vec3& v: vertices) v /= 2;
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
                scene.quads.append({uint4(base+A[0],base+A[1],base+A[2],base+A3),Image3f(4),Image3f(4)});
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
            const float v = -(float(y)/float(target.size.y-1)*2-1);
            for(uint x: range(target.size.x)) {
                const float u = float(x)/float(target.size.x-1)*2-1;
                const vec3 O = v0*(1-v)*(1-u) + v1*(1-v)*u + v2*v*u + v3*v*(1-u);
                bgr3f differentialOutgoingRadianceSum = 0; // Directional light
                const uint sampleCount = 64;
                for(uint unused i: range(sampleCount)) {
                    bgr3f differentialIncomingRadiance;
                    {
                        v8sf random8 = random();
                        const vec2 uv (random8[0], random8[1]);
                        const Scene::QuadLight light = scene.light;
                        const vec3 D = (light.O + uv[0] * light.T + uv[1] * light.B) - O;

                        const float dotAreaL = - dot(light.N, D);
                        //if(dotAreaL <= 0) return {false,0,0}; // Light sample behind face
                        const float dotNL = dot(D, N);
                        //if(dotNL <= 0) return {false,0,0};

                        bgr3f incomingRadiance = dotNL * dotAreaL / sq(sq(D)) * light.emissiveFlux; // Directional light

                        float nearestRealT = inff, nearestVirtualT = inff;
                        for(const Scene::Quad& quad: scene.quads) {
                            float u,v; vec3 N;
                            if(!quad.real) intersect(scene, quad, O, D, N, nearestVirtualT, u, v);
                            if( quad.real) intersect(scene, quad, O, D, N, nearestRealT, u, v);
                        }
                        differentialIncomingRadiance = nearestVirtualT < nearestRealT ? - incomingRadiance : 0;
                    }
                    const bgr3f albedo = 1;
                    differentialOutgoingRadianceSum += albedo * differentialIncomingRadiance;
                }
                target(x, y) = (1/float(sampleCount)) * differentialOutgoingRadianceSum;
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
        scene.quads.append(Scene::Quad{uint4(
                                       scene.vertices.add(vec3(-1,-1,0)), scene.vertices.add(vec3(1,-1,0)),
                                       scene.vertices.add(vec3(1,1,0)), scene.vertices.add(vec3(-1,1,0))),
                                       Image3f(4),Image3f(4)});

        //importSTL(scene, "Cube.stl");

        //step(scene);

        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;

        const float near = 3, far = near + 3; // FIXME: fit far
        const mat4 view = mat4().translate({0,0,-near-1}).rotateX(-π/4);

        Time time {true};
#if 1
        target.clear(byte4(0,0,0,0xFF));

        const mat4 projection = perspective(near, far); // .scale(vec3(1, W/H, 1))
        static constexpr int32 pixel = 16; // 11.4
        const mat4 NDC = mat4()
                .scale(vec3(vec2(target.size*uint(pixel/2u)), 1<<13)) // 0, 2 -> 11.4, .14
                .translate(vec3(1)); // -1, 1 -> 0, 2
        const mat4 M = NDC * projection * view;

        for(const Scene::Quad& quad: scene.quads) { // TODO: Z-Order
            int2 V[4];
            for(const uint i: range(4)) V[i] = int2((M * scene.vertices[quad.quad[i]]).xy());

            const int2 min = ::min<int2>(V);
            const int2 max = ::max<int2>(V);

            for(const uint y: range(::max(0, min.y/pixel), ::min<uint>(target.size.y, max.y/pixel))) {
                const uint Y = pixel*y + pixel/2;
                for(const uint x: range(::max(0, min.x/pixel), ::min<uint>(target.size.x, max.x/pixel))) {
                    const uint X = pixel*x + pixel/2;

                    int e01x = V[0].y - V[1].y, e01y = V[1].x - V[0].x, e01z = V[0].x * V[1].y - V[1].x * V[0].y;
                    e01z += (e01x>0/*dy<0*/ || (e01x==0/*dy=0*/ && e01y<0/*dx<0*/));
                    const int step01 = e01z + e01x*X + e01y*Y;
                    int e12x = V[1].y - V[2].y, e12y = V[2].x - V[1].x, e12z = V[1].x * V[2].y - V[2].x * V[1].y;
                    e12z += (e12x>0/*dy<0*/ || (e12x==0/*dy=0*/ && e12y<0/*dx<0*/));
                    const int step12 = e12z + e12x*X + e12y*Y;
                    int e23x = V[2].y - V[3].y, e23y = V[3].x - V[2].x, e23z = V[2].x * V[3].y - V[3].x * V[2].y;
                    e23z += (e23x>0/*dy<0*/ || (e23x==0/*dy=0*/ && e23y<0/*dx<0*/));
                    const int step23 = e23z + e23x*X + e23y*Y;
                    int e30x = V[3].y - V[0].y, e30y = V[0].x - V[3].x, e30z = V[3].x * V[0].y - V[0].x * V[3].y;
                    e30z += (e30x>0/*dy<0*/ || (e30x==0/*dy=0*/ && e30y<0/*dx<0*/));
                    const int step30 = e30z + e30x*X + e30y*Y;

                    if(step01>0 && step12>0 && step23>0 && step30>0) {
                        int e13x = V[1].y - V[3].y, e13y = V[3].x - V[1].x, e13z = V[1].x * V[3].y - V[3].x * V[1].y;
                        const int step13 = e13z + e13x*X + e13y*Y;
                        const int2 a0 = int2(0, 0);
                        const int2 a1 = int2(1, 0);
                        const int2 a2 = int2(1, 1);
                        const int2 a3 = int2(0, 1);
                        if(step13>0) {
                            const int2 e01a = a1 - a0;
                            const int2 e03a = a3 - a0;
                            const int2 Ex = e01x*e01a + e30x*e03a; // - ?
                            const int2 Ey = e01y*e01a + e30y*e03a;
                            const int2 Ez = e01z*e01a + e30z*e03a + a0;
                            const int area013 = e01z + e13z + e30z;
                            const vec2 a = vec2(Ez + Ex*int(X) + Ey*int(Y)) / float(area013); // FIXME
                            target(x, target.size.y-1-y) = byte4(0,0xFF*a.x,0xFF*a.y,0xFF);
                        } else {
                            target(x, target.size.y-1-y) = byte4(0xFF,0,0,0xFF);
                        }
                    }
                }
            }
        }
#else
        const vec3 O = view.inverse() * viewPosition;
        for(uint y: range(target.size.y)) {
            const float Dy = -(float(y)/float(target.size.y-1)*2-1);
            for(uint x: range(target.size.x)) {
                const float Dx = float(x)/float(target.size.x-1)*2-1;
                const vec3 D = normalize((mat3)(view.transpose()) * vec3(Dx, Dy, -near)); // Needs to be normalized for sphere intersection
                bgr3f realOutgoingRadiance = bgr3f(0, 0, 0); // FIXME: synthetic
                bgr3f differentialOutgoingRadiance = bgr3f(0, 0, 0);
                bgr3f virtualOutgoingRadiance = bgr3f(0, 0, 0);
                bool virtualHit = 0;
                float nearestRealT = inff;
                for(const Scene::Quad& quad: scene.quads) {
                    float u, v; vec3 N;
                    if(intersect(scene, quad, O, D, N, nearestRealT, u, v)) {
                        realOutgoingRadiance = bgr3f(0,v,u);
                        N = normalize(N);
                        const vec3 hitO = O + nearestRealT*D;
                        const uint2 texSize = quad.outgoingRadiance.size;
                        const uint x = (1+u)/2*(texSize.x-1);
                        const uint y = (1+v)/2*(texSize.y-1);
                        assert_(x<texSize.x && y<texSize.y, x, y, u, v);
                        differentialOutgoingRadiance = 0; //quad.outgoingRadiance(x, y); // FIXME: bilinear
                        const uint sampleCount = 1;
                        bgr3f outgoingRadianceSum = 0;
                        for(uint unused i: range(sampleCount)) {
                            bgr3f realIncomingRadiance;
                            {
                                const vec3 O = hitO;

                                v8sf random8 = random();
                                const vec2 uv (random8[0], random8[1]);
                                const Scene::QuadLight light = scene.light;
                                const vec3 D = (light.O + uv[0] * light.T + uv[1] * light.B) - O;

                                const float dotAreaL = - dot(light.N, D);
                                //if(dotAreaL <= 0) return {false,0,0}; // Light sample behind face
                                const float dotNL = dot(D, N);
                                //if(dotNL <= 0) return {false,0,0};

                                realIncomingRadiance = dotNL * dotAreaL / sq(sq(D)) * light.emissiveFlux; // Directional light

                                float nearestRealT = inff;
                                for(const Scene::Quad& quad: scene.quads) {
                                    float u,v; vec3 N;
                                    if( quad.real) if(intersect(scene, quad, O, D, N, nearestRealT, u, v)) realIncomingRadiance = 0;
                                }
                            }
                            const bgr3f albedo = 1;
                            outgoingRadianceSum += albedo * realIncomingRadiance;
                        }
                        const bgr3f outgoingRadiance = (1/float(sampleCount)) * outgoingRadianceSum;
                        if(!quad.real) { virtualHit=true; virtualOutgoingRadiance = outgoingRadiance; }
                        else { realOutgoingRadiance = outgoingRadiance; differentialOutgoingRadiance = 0; /*FIXME*/ }
                    }
                }
                const bgr3f finalOutgoingRadiance = mix(realOutgoingRadiance + differentialOutgoingRadiance, virtualOutgoingRadiance, virtualHit);
                target(x, y) = byte4(sRGB(finalOutgoingRadiance.b), sRGB(finalOutgoingRadiance.g), sRGB(finalOutgoingRadiance.r), 0xFF);
            }
        }
#endif
        log(time);
        //viewPosition += vec3(1./30,0,0);
        //window->render();
    }
} test;
