#include "prerender.h"
#include "thread.h"
#include "time.h"
#include "matrix.h"
#include "variant.h"
#include "camera.h"
#include "image.h"
#include "parallel.h"
#include "png.h"
#include "renderer/TraceableScene.h"
#include "integrators/TraceBase.h"
#include "sampling/UniformPathSampler.h"

struct Render {
    Render() {
        const Folder tmp {"/var/tmp/light",currentWorkingDirectory(), true};
        Folder folder {arguments()[0], tmp, true};
        //for(string file: folder.list(Files)) remove(file, folder);

        const int N = 2;
        uint2 size (960);
        const int spp = 1;

        File file(str(N)+'x'+str(N)+'x'+strx(size), folder, Flags(ReadWrite|Create));
        size_t byteSize = 4ull*N*N*size.y*size.x*sizeof(half);
        assert_(byteSize <= 28800ull*1024*1024, byteSize/(1024*1024*1024.f));
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
            if(stIndex && lastReport.seconds()>1) { log(strD(stIndex,N*N), int((N*N-stIndex)/stIndex*time.seconds()/60)); lastReport.reset(); }

            // Sheared perspective (rectification)
            const float s = 2*(sIndex/float(N-1))-1, t = 2*(tIndex/float(N-1))-1;
            mat4 projection; // Recover w by projecting from world to homogenous coordinates, as w cannot be recovered from normalized coordinates (after perspective divide: w/w=1)
            //s = angles.x/(PI/3), t = angles.y/(PI/3);
            vec4 O = camera * vec4(s, t, 0, 0);
            mat4 C1 = camera;
            C1.translate(vec3(s, t, 0));
            // Scales Z to keep UV plane independant of st
            C1(0, 2) -= O.x;
            C1(1, 2) -= O.y;
            C1(2, 2) -= O.z;
            projection(3, 3) = 0;
            projection(3, 2) = 1;

            ImageH Z (unsafeRef(field.slice(((0ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            ImageH B (unsafeRef(field.slice(((1ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            ImageH G (unsafeRef(field.slice(((2ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            ImageH R (unsafeRef(field.slice(((3ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            parallel_chunk(size.y, [&scene, C1, projection, size, &Z, &B, &G, &R](uint id, uint start, uint sizeI) {
                half* const targetZ = Z.begin();
                half* const targetB = B.begin();
                half* const targetG = G.begin();
                half* const targetR = R.begin();
                TraceBase tracer(scene, id);
                for(int y: range(start, start+sizeI)) for(uint x: range(size.x)) {
                    const vec4 Op = vec4((2.f*x/float(size.x-1)-1), ((2.f*y/float(size.y-1)-1)), 0, (projection*vec4(0,0,0,1)).w);
                    const vec3 O = C1 * (Op.w * Op.xyz());
                    const vec4 Pp = vec4((2.f*x/float(size.x-1)-1), ((2.f*y/float(size.y-1)-1)), 1, (projection*vec4(0,0,1,1)).w);
                    const vec3 P = C1 * (Pp.w * Pp.xyz());

                    float hitDistance;
                    Vec3f emission (0.f);
                    for(int unused i : range(spp)) emission += tracer.trace(O, P, hitDistance);
                    targetZ[y*size.x+x] = hitDistance / ::length(P-O); // (orthogonal) distance to ST plane
                    targetB[y*size.x+x] = emission[2] / spp;
                    targetG[y*size.x+x] = emission[1] / spp;
                    targetR[y*size.x+x] = emission[0] / spp;
                }
            });
#if 0 // DEBUG
            Image bgr (size);
            extern uint8 sRGB_forward[0x1000];
            for(uint svIndex: range(size.y)) for(uint suIndex: range(size.x)) {
                const uint index = svIndex*size.x+suIndex;
                const uint b = clamp(0u, uint(B[index]*0xFFF), 0xFFFu);
                const uint g = clamp(0u, uint(G[index]*0xFFF), 0xFFFu);
                const uint r = clamp(0u, uint(R[index]*0xFFF), 0xFFFu);
                bgr(suIndex, size.y-1-svIndex) = byte4(sRGB_forward[b], sRGB_forward[g], sRGB_forward[r], 0xFF);
            }
            writeFile(str(sIndex,tIndex)+".png", encodePNG(bgr), folder, true);
#endif
        }
        log("Rendered",strx(uint2(N)),"x",strx(size),"@",spp,"spp images in", time);
    }
} prerender;
