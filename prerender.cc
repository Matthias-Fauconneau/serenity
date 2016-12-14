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

        const int N = 9;
        uint2 size (256);

        File file(str(N)+'x'+str(N)+'x'+strx(size), cacheFolder, Flags(ReadWrite|Create));
        size_t byteSize = 4ull*N*N*size.y*size.x*sizeof(half);
        assert_(byteSize <= 16ull*1024*1024*1024);
        file.resize(byteSize);
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<half> field = mcast<half>(map);
        assert_(field.size == 4ull*N*N*size.y*size.x);
        //field.clear(); // Explicitly clears to avoid performance skew from clear on page faults (and forces memory allocation)

        const mat4 camera = parseCamera(readFile("scene.json"));

        TraceableScene scene;

        Time time (true); Time lastReport (true);
        for(int stIndex: range(N*N)) {
            int sIndex = stIndex%N, tIndex = stIndex/N;
            if(lastReport.seconds()>1) { log(strD(stIndex,N*N)); lastReport.reset(); }

            // Sheared perspective (rectification)
            const float s = 2*(sIndex/float(N-1))-1, t = 2*(tIndex/float(N-1))-1;
            //const mat4 M = shearedPerspective(s, t) * camera;
            parallel_chunk(size.y, [&scene, camera, s, t, size, tIndex, sIndex, field](uint id, uint start, uint sizeI) {
                ImageH Z (unsafeRef(field.slice(((0ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
                ImageH B (unsafeRef(field.slice(((1ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
                ImageH G (unsafeRef(field.slice(((2ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
                ImageH R (unsafeRef(field.slice(((3ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
                half* const targetZ = Z.begin();
                half* const targetB = B.begin();
                half* const targetG = G.begin();
                half* const targetR = R.begin();
                TraceBase tracer(scene, id);
                const vec3 O = camera * vec3(s, t, 0);
                for(int y: range(start, start+sizeI)) for(uint x: range(size.x)) {
                    const vec3 P = camera * vec3((2.f*x/float(size.x-1)-1), ((2.f*y/float(size.y-1)-1)), 1);
                    float hitDistance;
                    Vec3f emission = tracer.trace(O, P, hitDistance);
                    targetZ[y*size.x+x] = hitDistance;
                    targetB[y*size.x+x] = emission[2];
                    targetG[y*size.x+x] = emission[1];
                    targetR[y*size.x+x] = emission[0];
                }
            });
        }
        log("Rendered",strx(uint2(N)),"x",strx(size),"images in", time);
    }
} prerender;
