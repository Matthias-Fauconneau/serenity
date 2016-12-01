#pragma once
#include "radiosity.h"

struct Render {
    Scene& scene;
    Radiosity radiosity {scene};
    static constexpr uint sSize = 4, tSize = sSize; // Number of view-dependent samples along (s,t) dimensions
    Map map;

    Render(Scene& scene) : scene(scene) {
        const Folder& folder = Folder(basename(arguments()[0]), "/var/tmp/"_, true);
        assert_(Folder(".",folder).name() == "/var/tmp/"+basename(arguments()[0]), folder.name());
        for(string file: folder.list(Files)) remove(file, folder);

        const float detailCellCount = 32;

        // Fits face UV to maximum projected sample rate
        size_t sampleCount = 0;
        for(size_t faceIndex : range(scene.size/2)) { // FIXME: Assumes quads (TODO: generic triangle UV mapping)
            const vec3 p00 (scene.X0[2*faceIndex+0], scene.Y0[2*faceIndex+0], scene.Z0[2*faceIndex+0]);
            const vec3 p01 (scene.X1[2*faceIndex+0], scene.Y1[2*faceIndex+0], scene.Z1[2*faceIndex+0]);
            const vec3 p11 (scene.X2[2*faceIndex+0], scene.Y2[2*faceIndex+0], scene.Z2[2*faceIndex+0]);
            const vec3 p10 (scene.X2[2*faceIndex+1], scene.Y2[2*faceIndex+1], scene.Z2[2*faceIndex+1]);

            const vec3 faceCenter = (p00+p01+p11+p10)/4.f;
            const vec3 N = normalize(cross(p11-p00, p01-p00));
            // Viewpoint st with maximum projection area
            vec2 st = clamp(vec2(-1), scene.scale*faceCenter.xy() + (scene.scale*faceCenter.z/(N.z==0?0/*-0 negates infinities*/:-N.z))*N.xy(), vec2(1));
            if(!N.z) {
                if(!N.x) st.x = 0;
                if(!N.y) st.y = 0;
            }
            // Projects vertices along st view rays on uv plane (perspective)
            mat4 M = shearedPerspective(st[0], st[1], scene.near, scene.far);
            M.scale(scene.scale); // Fits scene within -1, 1
            const vec2 uv00 = (M*p00).xy();
            const vec2 uv01 = (M*p01).xy();
            const vec2 uv11 = (M*p11).xy();
            const vec2 uv10 = (M*p10).xy();
            const float maxU = ::max(length(uv01-uv00), length(uv11-uv10)); // Maximum projected edge length along quad's u axis
            const float maxV = ::max(length(uv10-uv00), length(uv11-uv01)); // Maximum projected edge length along quad's v axis

            const float cellCount = detailCellCount; //face.reflect ? detailCellCount : 1;
            const uint U = align(2, ceil(maxU*cellCount)), V = align(2, ceil(maxV*cellCount)); // Aligns UV to 2 for correct 32bit gather indexing
            assert_(U && V, p00, p01, p11, p10);

            // Allocates (s,t) (u,v) images
            scene.BGR[faceIndex*2+0] = scene.BGR[faceIndex*2+1] = 3*sampleCount;
            scene.size1[faceIndex*2+0] = scene.size1[faceIndex*2+1] = U;
            scene.size2[faceIndex*2+0] = scene.size2[faceIndex*2+1] = V*U;
            sampleCount += tSize*sSize*V*U;
            if(2*faceIndex+1 == scene.size-1) sampleCount += U; // Prevents OOB on interpolation
        }

        assert_(uint(detailCellCount) == detailCellCount);
        File file(str(uint(detailCellCount))+'x'+str(sSize)+'x'+str(tSize), folder, Flags(ReadWrite|Create));
        size_t byteSize = 3*sampleCount*sizeof(Float);
        assert_(byteSize <= 16ull*1024*1024*1024, byteSize/(1024*1024*1024.f));
        file.resize(byteSize);
        map = Map(file, Map::Prot(Map::Read|Map::Write));
        scene.base = mcast<Float>(map).begin();
    }
    void clear() { mcast<Float>(map).clear(0); scene.iterations=0; }
    void step() {
        Random randoms[threadCount()];
        for(Random& random: mref<Random>(randoms,threadCount())) { random=Random(); }
        {Random random; radiosity.lookup.generate(random);} // New set of stratified cosine samples for hemispheric rasterizer
        for(size_t faceIndex : range(scene.size/2)) { // FIXME: Assumes quads (TODO: generic triangle UV mapping)
            const vec3 p00 (scene.X0[2*faceIndex+0], scene.Y0[2*faceIndex+0], scene.Z0[2*faceIndex+0]);
            const vec3 p01 (scene.X1[2*faceIndex+0], scene.Y1[2*faceIndex+0], scene.Z1[2*faceIndex+0]);
            const vec3 p11 (scene.X2[2*faceIndex+0], scene.Y2[2*faceIndex+0], scene.Z2[2*faceIndex+0]);
            const vec3 p10 (scene.X2[2*faceIndex+1], scene.Y2[2*faceIndex+1], scene.Z2[2*faceIndex+1]);
            const vec3 t00 = (scene.TX0[2*faceIndex+0], scene.TY0[2*faceIndex+0], scene.TZ0[2*faceIndex+0]);
            const vec3 t01 = (scene.TX1[2*faceIndex+0], scene.TY1[2*faceIndex+0], scene.TZ1[2*faceIndex+0]);
            const vec3 t11 = (scene.TX2[2*faceIndex+0], scene.TY2[2*faceIndex+0], scene.TZ2[2*faceIndex+0]);
            const vec3 t10 = (scene.TX2[2*faceIndex+1], scene.TY2[2*faceIndex+1], scene.TZ0[2*faceIndex+1]);
            const vec3 b00 = (scene.BX0[2*faceIndex+0], scene.BY0[2*faceIndex+0], scene.BZ0[2*faceIndex+0]);
            const vec3 b01 = (scene.BX1[2*faceIndex+0], scene.BY1[2*faceIndex+0], scene.BZ1[2*faceIndex+0]);
            const vec3 b11 = (scene.BX2[2*faceIndex+0], scene.BY2[2*faceIndex+0], scene.BZ2[2*faceIndex+0]);
            const vec3 b10 = (scene.BX2[2*faceIndex+1], scene.BY2[2*faceIndex+1], scene.BZ0[2*faceIndex+1]);
            const vec3 n00 = (scene.NX0[2*faceIndex+0], scene.NY0[2*faceIndex+0], scene.NZ0[2*faceIndex+0]);
            const vec3 n01 = (scene.NX1[2*faceIndex+0], scene.NY1[2*faceIndex+0], scene.NZ1[2*faceIndex+0]);
            const vec3 n11 = (scene.NX2[2*faceIndex+0], scene.NY2[2*faceIndex+0], scene.NZ2[2*faceIndex+0]);
            const vec3 n10 = (scene.NX2[2*faceIndex+1], scene.NY2[2*faceIndex+1], scene.NZ0[2*faceIndex+1]);

            const uint U = scene.size1[faceIndex*2+0], VU = scene.size2[faceIndex*2+0], V =  V/U;
            Float* const faceBGR = scene.base + scene.BGR[faceIndex]; // base + index
            const size_t size4 = tSize*sSize*V*U;

            const vec3 ab = p01-p00;
            const vec3 ad = p10-p00;
            const vec3 badc = p00-p01+p11-p10;

            const vec3 Tab = t01-t00;
            const vec3 Tad = t10-t00;
            const vec3 Tbadc = t00-t01+t11-t10;

            const vec3 Bab = b01-b00;
            const vec3 Bad = b10-b00;
            const vec3 Bbadc = b00-b01+b11-b10;

            const vec3 Nab = n01-n00;
            const vec3 Nad = n10-n00;
            const vec3 Nbadc = n00-n01+n11-n10;

            // Shades surface
            parallel_for(0, V, [&](const uint id, const uint svIndex) {
                tsc totalTSC;
                totalTSC.start();
                for(uint suIndex: range(U)) {
                    const float v = (float(svIndex)+1.f/2)/float(V);
                    const float u = (float(suIndex)+1.f/2)/float(U);
                    const vec3 P = p00 + ad*v + (ab + badc*v) * u;
                    const vec3 T = t00 + Tad*v + (Tab + Tbadc*v) * u;
                    const vec3 B = b00 + Bad*v + (Bab + Bbadc*v) * u;
                    const vec3 N = n00 + Nad*v + (Nab + Nbadc*v) * u;
                    const size_t base0 = svIndex*U+suIndex;
                    /*if(scene.faces[faceIndex*2].reflect) {
                        for(uint t: range(tSize)) for(uint s: range(sSize)) {
                            const vec3 viewpoint = vec3((s/float(sSize-1))*2-1, (t/float(tSize-1))*2-1, 0)/scene.scale;
                            const vec3 D = normalize(P-viewpoint);
                            bgr3f color = scene.shade(faceIndex*2+0, P, D, T, B, N, randoms[id]);
                            const size_t base = base0 + (sSize * t + s) * VU;
                            faceBGR[0*size4+base] += color.b;
                            faceBGR[1*size4+base] += color.g;
                            faceBGR[2*size4+base] += color.r;
                        }
                    } else*/ {
                        const vec3 D = normalize(P);
                        bgr3f color = radiosity.shade(faceIndex*2+0, P, D, T, B, N, randoms[id]);
                        for(uint t: range(tSize)) for(uint s: range(sSize)) {
                            const size_t base = base0 + (sSize * t + s) * VU;
                            faceBGR[0*size4+base] += color.b;
                            faceBGR[1*size4+base] += color.g;
                            faceBGR[2*size4+base] += color.r;
                        }
                    }
                }
            });
        }
        scene.iterations++;
    }
};
