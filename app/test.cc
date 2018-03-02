#include "thread.h"
#if 1
#include "matrix.h"
#include "window.h"
#include "image-render.h"

generic T mix(const T& a, const T& b, const float t) { return (1-t)*a + t*b; }

struct Plane {
    vec3 P, T, B, N;
    Image3f differentialOutgoingRadiance;
};

struct Sphere { vec3 P; float r2; bgr3f albedo; bool real; };

struct Scene {
    array<Plane> planes;
    array<Sphere> spheres;
};

bool intersect(const Plane& plane, const vec3 O, const vec3 D, float& nearestT, float& u, float& v) {
    const vec3 P = plane.P - O;
    const vec3 T = plane.T;
    const vec3 B = plane.B;
    const vec3 N = plane.N;
    const float invdet = 1 / dot(N, D);
    const float t = invdet * dot(N, P);
    u = invdet * dot(cross(D, B), P);
    v = invdet * dot(cross(D, T), P);
    if(u >= -1 && u < 1) if(v >= -1 && v < 1) if(t < nearestT) {
        nearestT = t;
        return true;
    }
    return false;
}

bool intersect(const Sphere& sphere, const vec3 O, const vec3 D, float& nearestT, bgr3f& color) {
    const vec3 P = sphere.P - O;
    const float Δ = sphere.r2 - dot(P, P) + sq(dot(P, D));
    if(Δ > 0) {
        const float t = dot(P, D) - sqrt(Δ);
        if(t < nearestT) {
            color = sphere.albedo;
            nearestT = t;
            return true;
        }
    }
    return false;
}

static struct Test : Widget {
    unique<Window> window = ::window(this, 2048, mainThread, 0);
    Test() {
        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;

        const float near = 3;
        const mat4 view = mat4().translate({0,0,-near-1}).rotateX(-π/4);
        const vec3 O = view.inverse() * vec3(0,0,0);

        Scene scene;
        scene.planes.append(Plane{{0,0,0}, {1,0,0}, {0,1,0}, {0,0,1}, Image3f(512)});
        scene.spheres.append(Sphere{{-1./2,0,1./2}, sq(1./2), {1,0,0}, true});
        scene.spheres.append(Sphere{{+1./2,0,1./2}, sq(1./2), {1,0,0}, false});

        for(const Plane& plane: scene.planes) {
            const Image3f& target = plane.differentialOutgoingRadiance;
            for(uint y: range(target.size.y)) {
                const float v = -(float(y)/float(target.size.y-1)*2-1);
                for(uint x: range(target.size.x)) {
                    const float u = float(x)/float(target.size.x-1)*2-1;
                    const vec3 O = plane.P + v * plane.B + u * plane.T;
                    bgr3f differentialOutgoingRadiance; // Directional light
                    { // for(samples)
                        const vec3 D = vec3(0,0,1);
                        float nearestRealT = inff, nearestMixedT = inff;
                        bgr3f differentialIncomingRadiance;
                        {
                            bgr3f color;
                            bgr3f realIncomingRadiance = 1, mixedIncomingRadiance = 1; // Directional light
                            for(const Sphere sphere: scene.spheres) {
                                if(!sphere.real) if(intersect(sphere, O, D, nearestMixedT, color)) mixedIncomingRadiance = 0;
                                if( sphere.real) if(intersect(sphere, O, D, nearestRealT, color)) { realIncomingRadiance = 0; mixedIncomingRadiance = 0; }
                            }
                            differentialIncomingRadiance = mixedIncomingRadiance - realIncomingRadiance;
                        }
                        const bgr3f albedo = 1;
                        differentialOutgoingRadiance = albedo * differentialIncomingRadiance;
                    }
                    target(x, y) = differentialOutgoingRadiance;
                }
            }
        }


        for(uint y: range(target.size.y)) {
            const float Dy = -(float(y)/float(target.size.y-1)*2-1);
            for(uint x: range(target.size.x)) {
                const float Dx = float(x)/float(target.size.x-1)*2-1;
                //const vec3 D = normalize(view.inverse().normalMatrix() * vec3(Dx, Dy, -near)); // Needs to be normalized for sphere intersection
                const vec3 D = normalize((mat3)(view.transpose()) * vec3(Dx, Dy, -near)); // Needs to be normalized for sphere intersection
                bgr3f realOutgoingRadiance = bgr3f(0, 0, 0); // FIXME: synthetic
                bgr3f differentialOutgoingRadiance = bgr3f(0, 0, 0);
                bgr3f virtualOutgoingRadiance = bgr3f(0, 0, 0);
                bool unrealHit = 0;
                float nearestRealT = inff;
                for(const Plane& plane: scene.planes) {
                    float u, v;
                    if(intersect(plane, O, D, nearestRealT, u, v)) {
                        const uint2 texSize = plane.differentialOutgoingRadiance.size;
                        const uint x = (1+u)/2*(texSize.x-1);
                        const uint y = (1+v)/2*(texSize.y-1);
                        assert_(x<texSize.x && y<texSize.y, x, y, u, v);
                        differentialOutgoingRadiance = plane.differentialOutgoingRadiance(x, y); // FIXME: bilinear
                        { // for(samples)
                            bgr3f realIncomingRadiance = 1; // Directional light
                            const vec3 hitO = O + nearestRealT*D;
                            const vec3 L = vec3(0,0,1);
                            {
                                const vec3 O = hitO;
                                const vec3 D = L;
                                float nearestRealT = inff;
                                bgr3f color;
                                for(const Sphere sphere: scene.spheres) {
                                    if( sphere.real) if(intersect(sphere, O, D, nearestRealT, color)) realIncomingRadiance = 0;
                                }
                            }
                            const bgr3f albedo = 1;
                            realOutgoingRadiance = albedo * realIncomingRadiance;
                        }
                    }
                }
                for(const Sphere sphere: scene.spheres) {
                  if(!sphere.real) if(intersect(sphere, O, D, nearestRealT, virtualOutgoingRadiance)) unrealHit=true; // FIXME: nearestRealT
                  if( sphere.real) intersect(sphere, O, D, nearestRealT, realOutgoingRadiance);
                }
                //const bgr3f finalColor = mix(cameraInputColor + differentialColor, virtualOutgoingRadiance, unrealHit);
                const bgr3f finalOutgoingRadiance = realOutgoingRadiance + differentialOutgoingRadiance;
                target(x, y) = byte4(sRGB(finalOutgoingRadiance.b), sRGB(finalOutgoingRadiance.g), sRGB(finalOutgoingRadiance.r), 0xFF);
            }
        }
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
