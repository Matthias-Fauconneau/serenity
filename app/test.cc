#include "thread.h"
#if 1
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

inline bool intersect(const vec3 N, const vec3  e1v0, const vec3 _e2v0, const vec3 D, float& u, float& v, float& det) {
    u = dot(+e1v0, D);
    v = dot(_e2v0, D);
    if(!(max(u,v) <= 0)) return false; // u>0, v>0
    det = dot(N, D);
    if(!(det < 0)) return false; // Backward
    if(!((u + v) > det)) return false; // u+v<1
    return true;
}

inline bool intersect(const vec3 N, const vec3 e1, const vec3 e2, const vec3 v0, const vec3 D, float& u, float& v, float& rcpDet, float& t) {
    float det;
    if(intersect(N, cross(e1, v0), _cross(e2, v0), D, u, v, det)) {
        const float Nv0 = dot(N, v0);
        rcpDet = rcp( det );
        t = Nv0 * rcpDet;
        return true;
    }
    return false;
}

inline bool intersect(const vec3 v0, const vec3 v1, const vec3 v2, const vec3 O, const vec3 D, vec3& N, float& u, float& v, float& rcpDet, float& t) {
    const vec3 e1 = v2 - v0;
    const vec3 e2 = v1 - v0;
    N = cross(e2, e1);
    return intersect(N, e1, e2, v0-O, D, t, u, v, rcpDet);
}

bool intersect(const vec3 vA, const vec3 vB, const vec3 vC, const vec3 vD, const vec3 O, const vec3 D, vec3& N, float& nearestT, float& u, float& v) {
#if 0
    float rcpDet, t;
    if(intersect(a, b, c, O, D, N, u, v, rcpDet, t) && t < nearestT) {
        nearestT = t;
        u *= rcpDet;
        v *= rcpDet;
        return true;
    }
    if(intersect(a, c, d, O, D, N, u, v, rcpDet, t) && t < nearestT) {
        nearestT = t;
        u *= rcpDet;
        v *= rcpDet;
        u = 1 - u;
        v = 1 - v;
        return true;
    }
    return false;
#elif 1
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

    const float U0 = dot(eABvAO, D);
    const float V1 = dot(eBCvCO, D);
    const float U1 = dot(eCDvCO, D);
    const float V0 = dot(eDAcAO, D);
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
#elif 1
    const vec3 eBD = vD-vB; //+

    const vec3 eAB = vB-vA;
    const vec3 eBC = vC-vB;
    const vec3 eCD = vD-vC;
    const vec3 eDA = vA-vD;
    N = cross(eBC, eAB);

    const vec3 eBDvBO = _cross(eBD, vB-O); // +
    const float Nv0 = dot(N, vB-O);
    const vec3 eABvAO = _cross(eAB,vA-O);
    const vec3 eBCvCO = _cross(eBC,vC-O);
    const vec3 eCDvCO = _cross(eCD,vC-O);
    const vec3 eDAcAO = _cross(eDA,vA-O);

    const float W = dot(eBDvBO, D);
    const vec3 v0eU = select(W>0, eABvAO, eCDvCO);
    const vec3 v0eV = select(W>0, eDAcAO, eBCvCO);

    const float U = dot(v0eU, D);
    const float V = dot(v0eV, D);
    if(!(min(U,V) > 0)) return false;
    const float det = dot(N, D);
    if(!(det > 0)) return false;
    const float rcpDet = rcp( det );
    const float t = Nv0 * rcpDet;
    if(!(t < nearestT)) return false;
    nearestT = t;
    const float triU = rcpDet * U;
    const float triV = rcpDet * V;
    u = select(W > 0, triU, 1-triU);
    v = select(W > 0, triV, 1-triV);
    return true;
#else
    const vec3 eAC = vC-vA;
    const float W = dot(_cross(eAC, vA-O), D);
    const vec3 v0 = select(W>0, vB, vD);
    const vec3 v1 = select(W>0, vC, vA);
    const vec3 v2 = select(W>0, vA, vC);
    const vec3 e1 = v2-v0;
    const vec3 e2 = v0-v1;
    N = cross(e2,e1);
    const float Nv0 = dot(N, vA-O);
    const vec3 v0O = v0 - O;
    const float U = dot(cross(v0O,e1), D);
    const float V = dot(cross(v0O,e2), D);
    if(!(min(U,V) > 0)) return false;
    const float det = dot(N, D);
    if(!(det > 0)) return false;
    const float rcpDet = rcp( det );
    const float t = Nv0 * rcpDet;
    if(!(t < nearestT)) return false;
    nearestT = t;
    const float triU = rcpDet * U;
    const float triV = rcpDet * V;
    u = select(W > 0, triU, 1-triU);
    v = select(W > 0, triV, 1-triV);
    return true;
#endif
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
    while(!s.match("endsolid")) {
        s.whileAny(' ');
        s.skip("facet normal "); s.line(); // normal
        s.whileAny(' '); s.skip("outer loop\n");
        uint3 triangle;
        for(const uint triangleVertex: range(3)) {
            s.whileAny(' '); s.skip("vertex "); const vec3 v = parse<vec3>(s); s.skip('\n');
            const uint index = scene.vertices.add(v);
            triangle[triangleVertex] = index;
        }
        triangles.append(triangle);
        s.whileAny(' '); s.skip("endloop\n");
        s.whileAny(' '); s.skip("endfacet\n");
    }
    s.line(); // name
    assert_(!s);
    assert_(scene.vertices.size==8);

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
                scene.quads.append({uint4(A[0],A[1],A[2],A3),Image3f(4),Image3f(4)});
            }
        }
    }

    { // Rescale
        const vec3 min = ::min(scene.vertices);
        const vec3 max = ::max(scene.vertices);
        for(vec3& v: scene.vertices) v = (v-min)/(max-min)*2.f-vec3(1); // -> [-1, 1]
    }
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

        const float near = 3;
        const mat4 view = mat4().translate({0,0,-near-1}).rotateX(-π/4);
        const vec3 O = view.inverse() * viewPosition;

        {
            Time time {true};
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
                            realOutgoingRadiance = 1;
                            break;
                            N = normalize(N);
                            const vec3 hitO = O + nearestRealT*D;
                            const uint2 texSize = quad.outgoingRadiance.size;
                            const uint x = (1+u)/2*(texSize.x-1);
                            const uint y = (1+v)/2*(texSize.y-1);
                            assert_(x<texSize.x && y<texSize.y, x, y, u, v);
                            differentialOutgoingRadiance = quad.outgoingRadiance(x, y); // FIXME: bilinear
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
            log(time);
        }
        //viewPosition += vec3(1./30,0,0);
        //window->render();
    }
} test;

#elif 1
#include "quad.h"
static struct Test {
    Test() {
        buffer<Vertex> vertices (3*3);
        for(int i: range(3)) for(int j: range(3)) vertices[i*3+j] = {vec3(j, i, 0),0};
        buffer<Quad> quads (2*2);
        for(int i: range(2)) for(int j: range(2)) quads[i*2+j] = {uint4(i*3+j, i*3+j+1, (i+1)*3+j+1, (i+1)*3+j), 0};
        log(quads);
        buffer<Quad> low = decimate(quads, vertices);
        log(low);
    }
} test;
#else
#include "image.h"
#include "png.h"
#include "box.h"
#include "window.h"
#include "plot.h"

static double SSE(ref<rgb3f> A, ref<rgb3f> B) {
    double sse = 0;
    assert_(A.size == B.size);
    for(size_t i: range(A.size)) {
        sse += (double)sq(B[i][0] - A[i][0]);
        sse += (double)sq(B[i][1] - A[i][1]);
        sse += (double)sq(B[i][2] - A[i][2]);
    }
    return sse;
}

static struct App : Plot {
    App() {
        const ref<string> methods {"NoFilter"_};//, "Box3"};//, "Gaussian6"_, "Bilateral"_};

        const Folder folder ("box"_, environmentVariable("XDG_RUNTIME_DIR"));
        buffer<String> files = folder.list(Files);
        buffer<double> SSE(methods.size); SSE.clear(0);
        buffer<double> count(methods.size); count.clear(0);
        buffer<uint64> times(methods.size); times.clear(0);
        for(string file: files) {
            if(endsWith(file, ".png")) continue;
            TextData s (file);
            const uint quadIndex = s.integer();
            s.skip('.');
            const uint X = s.integer();
            s.skip('x');
            const uint Y = s.integer();
            s.skip('.');
            const uint Xω = s.integer();
            s.skip('x');
            const uint Yω = s.integer();
            if(s.match(".ref")) continue;
            assert_(!s);
            ::log(file);

            Map map (file, folder);
            ref<rgb3f> quadRadiosity = cast<rgb3f>(map);

            uint lastStep = 0;
            string refFile;
            for(string file: files) { // Ref
                if(endsWith(file, ".png")) continue;
                TextData s (file);
                const uint fileQuadIndex = s.integer();
                if(fileQuadIndex != quadIndex) continue;
                s.skip('.');
                const uint fileX = s.integer();
                assert_(fileX == X);
                s.skip('x');
                const uint fileY = s.integer();
                assert_(fileY == Y);
                s.skip('.');
                const uint fileXω = s.integer();
                assert_(fileXω == Xω);
                s.skip('x');
                const uint fileYω = s.integer();
                assert_(fileYω == Yω);
                if(!s.match(".ref")) continue;
                assert_(!s);
                refFile = file;
            }
            assert_(refFile);

            Map refMap (refFile, folder);
            ref<rgb3f> refRadiosity = cast<rgb3f>(refMap);

            for(size_t method: range(methods.size)) {
                buffer<rgb3f> filtered (quadRadiosity.size);
                //::log_(methods[method]+" "_);
                Time time {true};
                if(method == 0) filtered = unsafeRef(quadRadiosity);
                else if(method == 1) { // Box
                    constexpr int r = 3;
                    for(uint iω: range(Yω*Xω)) {
                        Image3f source (unsafeRef(quadRadiosity.slice(iω*Y*X,Y*X)),uint2(X,Y));
                        Image3f target (unsafeRef(filtered.slice(iω*Y*X,Y*X)),uint2(X,Y));
                        for(int y: range(Y)) for(int x: range(X)) {
                            rgb3f sum = 0;
                            for(int dy: range(-r, r +1)) for(int dx: range(-r, r +1)) {
                                sum += source(clamp<int>(0, x+dx, X-1), clamp<int>(0, y+dy, Y-1));
                            }
                            target(x, y) = sum / float(sq(r+1+r));
                        }
                    }
                }
                else if(method == 2) { // Gaussian
                    continue;
                    constexpr int radius = 6;
                    for(uint iω: range(Yω*Xω)) {
                        Image3f source (unsafeRef(quadRadiosity.slice(iω*Y*X,Y*X)),uint2(X,Y));
                        Image3f target (unsafeRef(filtered.slice(iω*Y*X,Y*X)),uint2(X,Y));
                        for(int y: range(Y)) for(int x: range(X)) {
                            rgb3f sum = 0;
                            float sumW = 0; // FIXME: constant
                            for(int dy: range(-radius, radius +1)) for(int dx: range(-radius, radius +1)) {
                                const float Rst2 = dx*dx+dy*dy;
                                const float Sst2 = sq(radius/(2*sqrt(2*ln(2))));
                                const float w = exp(-Rst2/Sst2);
                                sum += w * source(clamp<int>(0, x+dx, X-1), clamp<int>(0, y+dy, Y-1));
                                sumW += w;
                            }
                            target(x, y) = sum / sumW;
                        }
                    }
                } else if(method == 3) { // Bilateral
                    constexpr int radius = 6;
                    constexpr float σrange = 1;
                    //double sumD = 0;
                    for(uint iω: range(Yω*Xω)) {
                        Image3f source (unsafeRef(quadRadiosity.slice(iω*Y*X,Y*X)),uint2(X,Y));
                        Image3f target (unsafeRef(filtered.slice(iω*Y*X,Y*X)),uint2(X,Y));
                        for(int y: range(Y)) for(int x: range(X)) {
                            rgb3f sum = 0;
                            float sumW = 0; // FIXME: constant
                            const rgb3f O = source(x, y);
                            for(int dy: range(-radius, radius +1)) for(int dx: range(-radius, radius +1)) {
                                const float Rst2 = dx*dx+dy*dy;
                                const float Sst2 = sq(radius/(2*sqrt(2*ln(2))));
                                const rgb3f s = source(clamp<int>(0, x+dx, X-1), clamp<int>(0, y+dy, Y-1));
                                const float d2 = sq(s[0]-O[0])+sq(s[1]-O[1])+sq(s[2]-O[2]);
                                //sumD += d2;
                                const float w = exp(-Rst2/Sst2) * exp(-d2/σrange);
                                sum += w * s;
                                sumW += w;
                            }
                            target(x, y) = sum / sumW;
                        }
                    }
                    //::log(sumD / (X*Y*Yω*Xω));
                }
                times[method] += time.nanoseconds();
                //::log(time);
                double sse = ::SSE(filtered, refRadiosity);
                //assert_(isNumber(sse));
                if(isNumber(sse)) { SSE[method] += sse; count[method] += quadRadiosity.size; }
                if(method==0) {
                    for(uint iω: range(Yω*Xω)) {
                        writeFile(str(quadIndex)+"."+str(iω)+".orig.png",
                                  encodePNG(sRGB(Image3f(unsafeRef(refRadiosity.slice(iω*Y*X,Y*X)),uint2(X,Y)))), folder, true);
                        writeFile(str(quadIndex)+"."+str(iω)+".filter.png",
                                  encodePNG(sRGB(Image3f(unsafeRef(filtered.slice(iω*Y*X,Y*X)),uint2(X,Y)))), folder, true);
                    }
                }
            }
        }
        for(size_t method: range(methods.size)) {
            double mse = SSE[method] / count[method];
            ::log(mse / (SSE[1] / count[1]), methods[method], double(times[method])/second);
        }
    }
} app;
#endif
