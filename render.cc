#include "scene.h"
#include "parallel.h"
#include "file.h"
#include "png.h"

struct Render {
    Render() {
        Scene scene {::parseScene(readFile(sceneFile(basename(arguments()[0]))))};
        const Folder& folder = Folder(basename(arguments()[0]), "/var/tmp/"_, true);
        assert_(Folder(".",folder).name() == "/var/tmp/"+basename(arguments()[0]), folder.name());
        for(string file: folder.list(Files)) remove(file, folder);

        const float detailCellCount = 64;
        const uint sSize = 8, tSize = sSize; // Number of view-dependent samples along (s,t) dimensions
        assert_(sSize%8 == 0); // FIXME: 2x4 for better coherent ray early cull

        // Fits face UV to maximum projected sample rate
        size_t sampleCount = 0;
        for(size_t faceIndex : range(scene.faces.size)) {
            const vec3 A (scene.X[0][faceIndex], scene.Y[0][faceIndex], scene.Z[0][faceIndex]);
            const vec3 B (scene.X[1][faceIndex], scene.Y[1][faceIndex], scene.Z[1][faceIndex]);
            const vec3 C (scene.X[2][faceIndex], scene.Y[2][faceIndex], scene.Z[2][faceIndex]);
            const vec3 D (scene.X[3][faceIndex], scene.Y[3][faceIndex], scene.Z[3][faceIndex]);

            const vec3 faceCenter = (A+B+C+D)/4.f;
            const vec3 N = normalize(cross(C-A, B-A));
            // Viewpoint st with maximum projection area
            vec2 st = clamp(vec2(-1), scene.scale*faceCenter.xy() + (scene.scale*faceCenter.z/(N.z==0?0/*-0 negates infinities*/:-N.z))*N.xy(), vec2(1));
            if(!N.z) {
                if(!N.x) st.x = 0;
                if(!N.y) st.y = 0;
            }
            // Projects vertices along st view rays on uv plane (perspective)
            mat4 M = shearedPerspective(st[0], st[1], scene.near, scene.far);
            M.scale(scene.scale); // Fits scene within -1, 1
            const vec2 uvA = (M*A).xy();
            const vec2 uvB = (M*B).xy();
            const vec2 uvC = (M*C).xy();
            const vec2 uvD = (M*D).xy();
            const float maxU = ::max(length(uvB-uvA), length(uvC-uvD)); // Maximum projected edge length along quad's u axis
            const float maxV = ::max(length(uvD-uvA), length(uvC-uvB)); // Maximum projected edge length along quad's v axis

            Scene::Face& face = scene.faces[faceIndex];
            const float cellCount = detailCellCount; //face.reflect ? detailCellCount : 1;
            const uint U = align(2, ceil(maxU*cellCount)), V = align(2, ceil(maxV*cellCount)); // Aligns UV to 2 for correct 32bit gather indexing
            assert_(U && V);

            // Allocates (s,t) (u,v) images
            face.BGR = (half*)(3*sampleCount);
            face.size = uint2(U, V);
            sampleCount += tSize*sSize*V*U;
            if(faceIndex == scene.faces.size-1) sampleCount += U; // Prevents OOB on interpolation
        }

        assert_(uint(detailCellCount) == detailCellCount);
        File file(str(uint(detailCellCount))+'x'+str(sSize)+'x'+str(tSize), folder, Flags(ReadWrite|Create));
        size_t byteSize = 3*sampleCount*sizeof(half);
        assert_(byteSize <= 16ull*1024*1024*1024, byteSize/(1024*1024*1024.f));
        file.resize(byteSize);
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<half> BGR = mcast<half>(map);
        BGR.clear(0); // Explicitly clears to avoid performance skew from clear on page faults

        uint64 totalTime[threadCount()], innerTime[threadCount()];
        mref<uint64>(totalTime, threadCount()).clear(0);
        mref<uint64>(innerTime, threadCount()).clear(0);
        Random randoms[threadCount()];
        for(Random& random: mref<Random>(randoms,threadCount())) { random.seed(); }
        Time time {true};
        for(const size_t faceIndex: range(scene.faces.size)) {
            const vec3 A (scene.X[0][faceIndex], scene.Y[0][faceIndex], scene.Z[0][faceIndex]);
            const vec3 B (scene.X[1][faceIndex], scene.Y[1][faceIndex], scene.Z[1][faceIndex]);
            const vec3 C (scene.X[2][faceIndex], scene.Y[2][faceIndex], scene.Z[2][faceIndex]);
            const vec3 D (scene.X[3][faceIndex], scene.Y[3][faceIndex], scene.Z[3][faceIndex]);
            const vec3 nA = scene.faces[faceIndex].N[0];
            const vec3 nB = scene.faces[faceIndex].N[1];
            const vec3 nC = scene.faces[faceIndex].N[2];
            const vec3 nD = scene.faces[faceIndex].N[3];
            const Scene::Face& face = scene.faces[faceIndex];
            const uint U = face.size.x, V = face.size.y;
            half* const faceBGR = BGR.begin()+ (size_t)face.BGR; // base + index
            const size_t VU = V*U;
            const size_t size4 = tSize*sSize*V*U;
            half* const faceB = faceBGR+0*size4;
            half* const faceG = faceBGR+1*size4;
            half* const faceR = faceBGR+2*size4;

            const vec3 ab = B-A;
            const vec3 ad = D-A;
            const vec3 badc = A-B+C-D;

            const vec3 Nab = nB-nA;
            const vec3 Nad = nD-nA;
            const vec3 Nbadc = nA-nB+nC-nD;

            // Shades surface
            parallel_chunk(0, V, [=, &scene, &totalTime, &innerTime, &randoms](uint id, uint start, uint sizeI) {
                Random& random = randoms[id];
                tsc totalTSC, innerTSC;
                totalTSC.start();
                for(uint svIndex: range(start, start+sizeI)) for(uint suIndex: range(U)) {
                    const float v = (float(svIndex)+1.f/2)/float(V);
                    const float u = (float(suIndex)+1.f/2)/float(U);
                    const vec3 O = A + ad*v + (ab + badc*v) * u;
                    const vec3 N = nA + Nad*v + (Nab + Nbadc*v) * u;
                    const size_t base0 = svIndex*U+suIndex;
                    if(!face.reflect) {
                        // Shader view-independent evaluation
                        const uint sampleCount = 4096;
                        bgr3f color = 0;
                        for(uint unused sampleIndex: range(sampleCount)) {
                            // Light
                            size_t light = scene.lights[0];
                            if(faceIndex == light) color += 1;
                            else {
                                const float u = random();
                                const float v = random();
                                vec3 L = normalize(vec3(scene.X[0][light] + u * (scene.X[1][light]-scene.X[0][light]) + v * (scene.X[3][light]-scene.X[0][light]),
                                                        scene.Y[0][light] + u * (scene.Y[1][light]-scene.Y[0][light]) + v * (scene.Y[3][light]-scene.Y[0][light]),
                                                        scene.Z[0][light] + u * (scene.Z[1][light]-scene.Z[0][light]) + v * (scene.Z[3][light]-scene.Z[0][light]))-O);
                                float dotNL = dot(N, L);
                                //color = max(0.f,  dotNL); break;
                                if(dotNL <= 0) color += 0;
                                else {
                                    size_t lightRayHitFaceIndex = scene.raycast(O, L);
                                    if(lightRayHitFaceIndex != light) color += 0;
                                    else color += vec3(scene.B[faceIndex], scene.G[faceIndex], scene.R[faceIndex]);
                                }
                            }
                        }
                        //color = (O-scene.min)/(scene.max-scene.min);
                        // FIXME: TODO: Mixed view-independent / view-dependent shaders
                        for(uint t: range(tSize)) for(uint s: range(sSize)) {
                            const size_t base = base0 + (sSize * t + s) * VU;
                            faceBGR[0*size4+base] = color.b/sampleCount;
                            faceBGR[1*size4+base] = color.g/sampleCount;
                            faceBGR[2*size4+base] = color.r/sampleCount;
                        }
                    } else {
                        innerTSC.start();
                        const v8sf Ox = O.x;
                        const v8sf Oy = O.y;
                        const v8sf Oz = O.z;

                        // FIXME: TODO: Mixed view-independent / view-dependent shaders

                        // Shader final view-dependent evaluation
#if 0
                        static constexpr v8sf seqF = v8sf{0,1,2,3,4,5,6,7};
                        const float Dx0 = O.x+1./scene.scale;
                        const float Dy0 = O.y+1./scene.scale;
                        const float Dz = O.z;
                        const float Dxs = -2/((sSize-1)*scene.scale);
                        const float Dyt = -2/((tSize-1)*scene.scale);
                        const float RxDx = 1 - 2*N.x*N.x;
                        const float RyDx =   - 2*N.x*N.y;
                        const float RzDx =   - 2*N.x*N.z;
                        const float a0 = -2*N.z*Dz;

                        const float Rx00 = a0*N.x + (N.x*N.y+0)*Dy0 + RxDx*Dx0;
                        const float Rx0t = (N.x*N.y+0)*Dyt;
                        const float Ry00 = a0*N.y + (N.y*N.y+1)*Dy0 + RyDx*Dx0;
                        const float Ry0t = (N.y*N.y+1)*Dyt;
                        const float Rz00 = a0*N.z + (N.z*N.y+1)*Dy0 + RzDx*Dx0;
                        const float Rz0t = (N.z*N.y+1)*Dyt;

                        const v8sf Rxs = RxDx * Dxs;
                        const v8sf Rys = RyDx * Dxs;
                        const v8sf Rzs = RzDx * Dxs;
                        for(uint t: range(tSize)) {
                            const v8sf Rx0 = Rx00 + Rx0t*float(t);
                            const v8sf Ry0 = Ry00 + Ry0t*float(t);
                            const v8sf Rz0 = Rz00 + Rz0t*float(t);
                            const size_t baseT = base0 + (sSize * t) * VU;
                            for(uint s=0; s<sSize; s += 8) {

                                const v8sf S = float8(float(s))+seqF;
                                const v8sf Rx = Rx0 + Rxs * S;
                                const v8sf Ry = Ry0 + Rys * S;
                                const v8sf Rz = Rz0 + Rzs * S;
#else
                        for(uint t: range(tSize)) {
                            const size_t baseT = base0 + (sSize * t) * VU;
                            for(uint s=0; s<sSize; s += 8) {
                                v8sf sumB = 0, sumG = 0, sumR = 0;
                                const uint sampleCount = 64;
                                for(uint unused sampleIndex: range(sampleCount)) {
                                    v8sf Rx, Ry, Rz;
                                    for(int k: range(8)) {
                                        const vec3 viewpoint = vec3(((s+k)/float(sSize-1))*2-1, (t/float(tSize-1))*2-1, 0)/scene.scale;
                                        const vec3 D = (O-viewpoint);
                                        //const vec3 R = D - 2*dot(N, D)*N;
                                        // Marsaglia
                                        float t0, t1, sq;
                                        do {
                                            t0 = random()*2-1;
                                            t1 = random()*2-1;
                                            sq = t0*t0 + t1*t1;
                                        } while(sq >= 1);
                                        float r = sqrt(1-sq);
                                        vec3 U = vec3(2*t0*r, 2*t1*r, 1-2*sq);
                                        if(dot(U,N) < 0) U = -U; // On hemisphere
                                        const vec3 R = normalize(normalize(D - 2*dot(N, D)*N) + face.gloss * U);
                                        Rx[k] = R.x;
                                        Ry[k] = R.y;
                                        Rz[k] = R.z;
                                    }
#endif
                                    const v8si index = scene.raycast(Ox,Oy,Oz, Rx,Ry,Rz);
                                    // FIXME: shader evaluation (i.e with lighting)
                                    const v8sf B =           gather(scene.B.data, index);
                                    const v8sf G = (1.f/2) * gather(scene.G.data, index);
                                    const v8sf R = (1.f/2) * gather(scene.R.data, index);
                                    sumB += B;
                                    sumG += G;
                                    sumR += R;
                                }
                                const v8hf b = toHalf(sumB/sampleCount);
                                const v8hf g = toHalf(sumG/sampleCount);
                                const v8hf r = toHalf(sumR/sampleCount);
                                const size_t base = baseT + s * VU;
                                for(uint k: range(8)) faceB[base + k*VU] = b[k];
                                for(uint k: range(8)) faceG[base + k*VU] = g[k];
                                for(uint k: range(8)) faceR[base + k*VU] = r[k];
                            }
                        }
                        innerTSC.stop();
                    }
                }
                totalTime[id] += totalTSC.cycleCount();
                innerTime[id] += innerTSC.cycleCount();
            });
        }
        //assert_(sum(ref<uint64>(innerTime, threadCount()))*100 >= 99*sum(ref<uint64>(totalTime, threadCount())));
        log(sampleCount/(1024*1024*1024.f), "G samples in", time, "=", str((float)time.nanoseconds()/sampleCount, 1u), "ns/sample");
#if 0 // DEBUG
        for(const size_t faceIndex: range(scene.faces.size)) {
            const Scene::Face& face = scene.faces[faceIndex];
            const uint U = face.size.x, V = face.size.y;
            const uint VU = V*U;
            const uint size4 = tSize*sSize*VU;
            const half* const faceBGR = BGR.begin()+ (size_t)face.BGR; // base + index
            Image bgr (sSize*U, tSize*V);
            extern uint8 sRGB_forward[0x1000];
            for(uint svIndex: range(V)) for(uint suIndex: range(U)) for(uint t: range(tSize)) for(uint s: range(sSize)) {
                const uint index = (sSize * t + s)*VU + (svIndex*U+suIndex);
                const uint B = clamp(0u, uint(faceBGR[0*size4+index]*0xFFF), 0xFFFu);
                const uint G = clamp(0u, uint(faceBGR[1*size4+index]*0xFFF), 0xFFFu);
                const uint R = clamp(0u, uint(faceBGR[2*size4+index]*0xFFF), 0xFFFu);
                bgr(s*U+suIndex, t*V+svIndex) = byte4(sRGB_forward[B], sRGB_forward[G], sRGB_forward[R], 0xFF);
            }
            writeFile(str(faceIndex)+".png", encodePNG(bgr), folder);
        }
#endif
    }
} render;
