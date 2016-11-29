#include "scene.h"
#include "parallel.h"
#include "file.h"
#include "png.h"

void report(uint64 totalTime, const Scene::Timers timers[], const uint bounce, Scene::Path path, const uint offset, const uint stride) {
    if(bounce > 1) return;
    for(Scene::Bounce type: {Scene::Direct, Scene::Diffuse, Scene::Specular}) {
        path[bounce] = type;
        array<char> s; for(uint i: range(bounce+1)) s.append(" .DS"_[path[i]]);
        uint64 sum = 0; for(uint i: range(threadCount())) sum += timers[i][offset+type*stride];
        if(sum) log(s, strD(sum,totalTime));
        report(totalTime, timers, bounce+1, path, type*stride, stride*Scene::Max);
    }
}

struct Render {
    Render() {
        Scene scene {::parseScene(readFile(sceneFile(basename(arguments()[0]))))};
        const Folder& folder = Folder(basename(arguments()[0]), "/var/tmp/"_, true);
        assert_(Folder(".",folder).name() == "/var/tmp/"+basename(arguments()[0]), folder.name());
        for(string file: folder.list(Files)) remove(file, folder);

        const float detailCellCount = 32;
        const uint sSize = 4, tSize = sSize; // Number of view-dependent samples along (s,t) dimensions
        //assert_(sSize%8 == 0); // FIXME: 2x4 for better coherent ray early cull

        // Fits face UV to maximum projected sample rate
        size_t sampleCount = 0;
        for(size_t faceIndex : range(scene.faces.size/2)) { // FIXME: Assumes quads (TODO: generic triangle UV mapping)
            const vec3 A (scene.X[0][2*faceIndex+0], scene.Y[0][2*faceIndex+0], scene.Z[0][2*faceIndex+0]);
            const vec3 B (scene.X[1][2*faceIndex+0], scene.Y[1][2*faceIndex+0], scene.Z[1][2*faceIndex+0]);
            const vec3 C (scene.X[2][2*faceIndex+0], scene.Y[2][2*faceIndex+0], scene.Z[2][2*faceIndex+0]);
            const vec3 D (scene.X[2][2*faceIndex+1], scene.Y[2][2*faceIndex+1], scene.Z[2][2*faceIndex+1]);

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

            Scene::Face& face = scene.faces[2*faceIndex+0];
            const float cellCount = detailCellCount; //face.reflect ? detailCellCount : 1;
            const uint U = align(2, ceil(maxU*cellCount)), V = align(2, ceil(maxV*cellCount)); // Aligns UV to 2 for correct 32bit gather indexing
            assert_(U && V, A, B, C, D);

            // Allocates (s,t) (u,v) images
            face.BGR = (Float*)(3*sampleCount);
            face.size = uint2(U, V);
            sampleCount += tSize*sSize*V*U;
            if(2*faceIndex+1 == scene.faces.size-1) sampleCount += U; // Prevents OOB on interpolation
        }

        assert_(uint(detailCellCount) == detailCellCount);
        File file(str(uint(detailCellCount))+'x'+str(sSize)+'x'+str(tSize), folder, Flags(ReadWrite|Create));
        size_t byteSize = 3*sampleCount*sizeof(Float);
        assert_(byteSize <= 16ull*1024*1024*1024, byteSize/(1024*1024*1024.f));
        file.resize(byteSize);
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<Float> BGR = mcast<Float>(map);
        BGR.clear(0); // Explicitly clears to avoid performance skew from clear on page faults (and for progressive rendering sum) (FIXME: intermediate sum on Float)

        uint64 totalTime[threadCount()];
        Scene::Timers timers[threadCount()];
        mref<uint64>(totalTime, threadCount()).clear(0);
        for(auto& timers: mref<Scene::Timers>(timers, threadCount())) mref<uint64>(timers, Scene::Max*Scene::Max*Scene::Max).clear(0);
        Random randoms[threadCount()];
        for(Random& random: mref<Random>(randoms,threadCount())) { random=Random(); }
        Time time {true};
        const uint iterations = 1;
        for(uint unused i: range(iterations)) {
            {Random random; for(Lookup& lookup: scene.lookups) lookup.generate(random);} // New set of stratified cosine samples for hemispheric rasterizer
            for(const size_t faceIndex: range(scene.faces.size/2)) { // FIXME: Assumes quads (TODO: generic triangle UV mapping)
                const vec3 A (scene.X[0][2*faceIndex+0], scene.Y[0][2*faceIndex+0], scene.Z[0][2*faceIndex+0]);
                const vec3 B (scene.X[1][2*faceIndex+0], scene.Y[1][2*faceIndex+0], scene.Z[1][2*faceIndex+0]);
                const vec3 C (scene.X[2][2*faceIndex+0], scene.Y[2][2*faceIndex+0], scene.Z[2][2*faceIndex+0]);
                const vec3 D (scene.X[2][2*faceIndex+1], scene.Y[2][2*faceIndex+1], scene.Z[2][2*faceIndex+1]);
                const vec3 tA = scene.faces[2*faceIndex+0].T[0];
                const vec3 tB = scene.faces[2*faceIndex+0].T[1];
                const vec3 tC = scene.faces[2*faceIndex+0].T[2];
                const vec3 tD = scene.faces[2*faceIndex+1].T[2];
                const vec3 bA = scene.faces[2*faceIndex+0].B[0];
                const vec3 bB = scene.faces[2*faceIndex+0].B[1];
                const vec3 bC = scene.faces[2*faceIndex+0].B[2];
                const vec3 bD = scene.faces[2*faceIndex+1].B[2];
                const vec3 nA = scene.faces[2*faceIndex+0].N[0];
                const vec3 nB = scene.faces[2*faceIndex+0].N[1];
                const vec3 nC = scene.faces[2*faceIndex+0].N[2];
                const vec3 nD = scene.faces[2*faceIndex+1].N[2];
                const Scene::Face& face = scene.faces[2*faceIndex+0];
                const uint U = face.size.x, V = face.size.y;
                Float* const faceBGR = BGR.begin()+ (size_t)face.BGR; // base + index
                const size_t VU = V*U;
                const size_t size4 = tSize*sSize*V*U;

                const vec3 ab = B-A;
                const vec3 ad = D-A;
                const vec3 badc = A-B+C-D;

                const vec3 Tab = tB-tA;
                const vec3 Tad = tD-tA;
                const vec3 Tbadc = tA-tB+tC-tD;

                const vec3 Bab = bB-bA;
                const vec3 Bad = bD-bA;
                const vec3 Bbadc = bA-bB+bC-bD;

                const vec3 Nab = nB-nA;
                const vec3 Nad = nD-nA;
                const vec3 Nbadc = nA-nB+nC-nD;

                // Shades surface
                //parallel_chunk(0, V, [=, &scene, &totalTime, &innerTime, &randoms](const uint id, const uint start, const uint sizeI) {
                parallel_for(0, V, [&](const uint id, const uint svIndex) {
                    tsc totalTSC;
                    totalTSC.start();
                    for(uint suIndex: range(U)) {
                        const float v = (float(svIndex)+1.f/2)/float(V);
                        const float u = (float(suIndex)+1.f/2)/float(U);
                        const vec3 P = A + ad*v + (ab + badc*v) * u;
                        const vec3 T = tA + Tad*v + (Tab + Tbadc*v) * u;
                        const vec3 B = bA + Bad*v + (Bab + Bbadc*v) * u;
                        const vec3 N = nA + Nad*v + (Nab + Nbadc*v) * u;
                        const size_t base0 = svIndex*U+suIndex;
                        if(scene.faces[faceIndex*2].reflect) {
                            for(uint t: range(tSize)) for(uint s: range(sSize)) {
                                const vec3 viewpoint = vec3((s/float(sSize-1))*2-1, (t/float(tSize-1))*2-1, 0)/scene.scale;
                                const vec3 D = normalize(P-viewpoint);
                                Scene::Path path;
                                bgr3f color = scene.shade(faceIndex*2+0, P, D, T, B, N, randoms[id], 0, path, timers[id], 1);
                                const size_t base = base0 + (sSize * t + s) * VU;
                                faceBGR[0*size4+base] += color.b;
                                faceBGR[1*size4+base] += color.g;
                                faceBGR[2*size4+base] += color.r;
                            }
                        } else {
                            const vec3 D = normalize(P);
                            Scene::Path path;
                            bgr3f color = scene.shade(faceIndex*2+0, P, D, T, B, N, randoms[id], 0, path, timers[id], 1);
                            for(uint t: range(tSize)) for(uint s: range(sSize)) {
                                const size_t base = base0 + (sSize * t + s) * VU;
                                faceBGR[0*size4+base] += color.b;
                                faceBGR[1*size4+base] += color.g;
                                faceBGR[2*size4+base] += color.r;
                            }
                        }
                    }
                    totalTime[id] += totalTSC.cycleCount();
                });
            }
        }
        for(Float& v: BGR) v /= iterations;
        Scene::Path path;
        report(::sum(ref<uint64>(totalTime, threadCount())), timers, 0, path, 0, 1);
        //assert_(sum(ref<uint64>(innerTime, threadCount()))*100 >= 99*sum(ref<uint64>(totalTime, threadCount())));
        log(sampleCount/(1024*1024*1024.f), "G samples in", time, "=", str((float)time.nanoseconds()/sampleCount, 1u), "ns/sample");
        log(scene.count/(1024*1024*1024.f), "G rasterizations in", time, "=", str((float)time.nanoseconds()/sampleCount, 1u), "ns/R");
#if 0 // DEBUG
        for(const size_t faceIndex: range(scene.faces.size/2)) {
            const Scene::Face& face = scene.faces[faceIndex*2+0];
            const uint U = face.size.x, V = face.size.y;
            const uint VU = V*U;
            const uint size4 = tSize*sSize*VU;
            const Float* const faceBGR = BGR.begin()+ (size_t)face.BGR; // base + index
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
