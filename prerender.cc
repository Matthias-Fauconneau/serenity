#include "prerender.h"
#include "thread.h"
#include "time.h"
#include "matrix.h"
#include "variant.h"
#include "camera.h"
#include "image.h"
#include "parallel.h"
#include "renderer/TraceableScene.h"
#include "integrators/TraceBase.h"
#include "sampling/UniformPathSampler.h"

struct Render {
    Render() {
        const Folder tmp {"/var/tmp/light",currentWorkingDirectory(), true};
        Folder cacheFolder {"teapot", tmp, true};
        for(string file: cacheFolder.list(Files)) remove(file, cacheFolder);

        const int N = 17;
        uint2 size (512);

        File file(str(N)+'x'+str(N)+'x'+strx(size), cacheFolder, Flags(ReadWrite|Create));
        size_t byteSize = 4ull*N*N*size.y*size.x*sizeof(half);
        assert_(byteSize <= 16ull*1024*1024*1024);
        file.resize(byteSize);
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<half> field = mcast<half>(map);
        assert_(field.size == 4ull*N*N*size.y*size.x);
        //field.clear(); // Explicitly clears to avoid performance skew from clear on page faults (and forces memory allocation)

        mat4 camera = parseCamera(readFile("scene.json"));

        TraceableScene scene;

        Time time (true); Time lastReport (true);
        //parallel_for(0, N*N, [&](uint unused threadID, size_t stIndex) {
        for(int stIndex: range(N*N)) {
            int sIndex = stIndex%N, tIndex = stIndex/N;
            if(lastReport.seconds()>1) { log(strD(stIndex,N*N)); lastReport.reset(); }

            // Sheared perspective (rectification)
            const float s = sIndex/float(N-1), t = tIndex/float(N-1);
            const mat4 M = shearedPerspective(s, t) * camera;
            parallel_chunk(size.y, [&scene, M, size, tIndex, sIndex, field](uint, uint start, uint sizeI) {
                ImageH Z (unsafeRef(field.slice(((0ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
                ImageH B (unsafeRef(field.slice(((1ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
                ImageH G (unsafeRef(field.slice(((2ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
                ImageH R (unsafeRef(field.slice(((3ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
                half* const targetZ = Z.begin();
                half* const targetB = B.begin();
                half* const targetG = G.begin();
                half* const targetR = R.begin();
                UniformPathSampler sampler(readCycleCounter());
                TraceBase tracer(&scene, TraceSettings(), 0);
                for(int y: range(start, start+sizeI)) for(uint x: range(size.x)) {
                    uint pixelIndex = y*size.x + x;
                    sampler.startPath(pixelIndex, 0);
                    const vec3 O = M.inverse() * vec3(2.f*x/float(size.x-1)-1, -(2.f*y/float(size.y-1)-1), -1);
                    PositionSample position;
                    position.p.x() = O.x;
                    position.p.y() = O.y;
                    position.p.z() = O.z;
                    position.weight = Vec3f(1.0f);
                    position.pdf = 1.0f;
                    position.Ng = scene.camera()->_transform.fwd();
                    DirectionSample direction;
                    const vec3 P = M.inverse() * vec3(2.f*x/float(size.x-1)-1, -(2.f*y/float(size.y-1)-1), +1);
                    const vec3 d = normalize(P-O);
                    direction.d.x() = d.x;
                    direction.d.y() = d.y;
                    direction.d.z() = d.z;
                    direction.weight = Vec3f(1.0f);
                    direction.pdf = 1;

                    Vec3f throughput (1);
                    Ray ray(position.p, direction.d);
                    ray.setPrimaryRay(true);

                    MediumSample mediumSample;
                    SurfaceScatterEvent surfaceEvent;
                    IntersectionTemporary data;
                    Medium::MediumState state;
                    state.reset();
                    IntersectionInfo info;
                    Vec3f emission(0.0f);
                    constexpr int maxBounces = 16;
                    const Medium *medium = scene.camera()->medium().get();

                    float hitDistance = 0.0f;
                    int bounce = 0;
                    bool didHit = scene.intersect(ray, data, info);
                    bool wasSpecular = true;
                    while ((didHit || medium) && bounce < maxBounces) {
                        bool hitSurface = true;
                        if (medium) {
                            if (!medium->sampleDistance(sampler, ray, state, mediumSample))
                                goto done;
                            throughput *= mediumSample.weight;
                            hitSurface = mediumSample.exited;
                            if (hitSurface && !didHit)
                                break;
                        }

                        if (hitSurface) {
                            hitDistance += ray.farT();

                            surfaceEvent = tracer.makeLocalScatterEvent(data, info, ray, &sampler);
                            Vec3f transmittance(-1.0f);
                            bool terminate = !tracer.handleSurface(surfaceEvent, data, info, medium, bounce, false, true, ray, throughput, emission, wasSpecular, state, &transmittance);

                            if (terminate)
                                goto done;
                        } else {
                            if (!tracer.handleVolume(sampler, mediumSample, medium, bounce, false, true, ray, throughput, emission, wasSpecular))
                                goto done;
                        }

                        if (throughput.max() == 0.0f)
                            break;

                        float roulettePdf = std::abs(throughput).max();
                        if (bounce > 2 && roulettePdf < 0.1f) {
                            if (sampler.nextBoolean(roulettePdf))
                                throughput /= roulettePdf;
                            else
                                goto done;
                        }

                        bounce++;
                        if (bounce < maxBounces)
                            didHit = scene.intersect(ray, data, info);
                    }
                    if (bounce < maxBounces)
                        tracer.handleInfiniteLights(data, info, true, ray, throughput, wasSpecular, emission);
                    done:;
                    targetZ[pixelIndex] = hitDistance;
                    targetB[pixelIndex] = emission[2];
                    targetG[pixelIndex] = emission[1];
                    targetR[pixelIndex] = emission[0];
                }
            });
        }
        log("Rendered",strx(uint2(N)),"x",strx(size),"images in", time);
    }
} prerender;
