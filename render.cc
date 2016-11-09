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

#if 1 // Surface parametrized render
        const float detailCellCount = 256;
        const uint sSize = 16, tSize = sSize; // Number of view-dependent samples along (s,t) dimensions
        assert_(sSize%8 == 0); // FIXME: 2x4 for better coherent ray early cull
        const uint stSize = tSize*sSize;


        // Fits face UV to maximum projected sample rate
        size_t sampleCount = 0;
        for(size_t i : range(scene.faces.size)) {
            const vec3 A (scene.X[0][i], scene.Y[0][i], scene.Z[0][i]);
            const vec3 B (scene.X[1][i], scene.Y[1][i], scene.Z[1][i]);
            const vec3 C (scene.X[2][i], scene.Y[2][i], scene.Z[2][i]);
            const vec3 D (scene.X[3][i], scene.Y[3][i], scene.Z[3][i]);

            const vec3 faceCenter = (A+B+C+D)/4.f;
            const vec3 N = normalize(cross(C-A, B-A));
            // Viewpoint st with maximum projection
            vec2 st = clamp(vec2(-1),
                            scene.scale*(faceCenter.xy()-scene.viewpoint.xy()) + (scene.scale*(faceCenter.z-scene.viewpoint.z)/(N.z==0?0/*-0 negates infinities*/:-N.z))*N.xy(),
                            vec2(1));
            if(!N.z) {
                if(!N.x) st.x = 0;
                if(!N.y) st.y = 0;
            }
            // Projects vertices along st view rays on uv plane (perspective)
            mat4 M = shearedPerspective(st[0], st[1], scene.near, scene.far);
            M.scale(scene.scale); // Fits scene within -1, 1
            M.translate(-scene.viewpoint);
            const vec2 uvA = (M*A).xy();
            const vec2 uvB = (M*B).xy();
            const vec2 uvC = (M*C).xy();
            const vec2 uvD = (M*D).xy();
            const float maxU = ::max(length(uvB-uvA), length(uvC-uvD)); // Maximum projected edge length along quad's u axis
            const float maxV = ::max(length(uvD-uvA), length(uvC-uvB)); // Maximum projected edge length along quad's v axis

            Scene::Face& face = scene.faces[i];
            const float cellCount = face.attributes.reflect ? detailCellCount : 1;
            const uint U = align(2, ceil(maxU*cellCount)), V = align(2, ceil(maxV*cellCount)); // Aligns UV to 2 for correct 32bit gather indexing
            assert_(U && V);

            // Allocates (s,t) (u,v) images
            face.attributes.BGR = (half*)sampleCount;
            sampleCount += 3*tSize*sSize*V*U;
        }

        assert_(uint(detailCellCount) == detailCellCount);
        File file(str(uint(detailCellCount))+'x'+str(sSize)+'x'+str(tSize), folder, Flags(ReadWrite|Create));
        size_t byteSize = sampleCount*sizeof(half);
        log(strx(uint2(sSize, tSize)), "=", sampleCount, "samples (per component)", "=", byteSize/1024/1024.f, "M");
        assert_(byteSize <= 16ull*1024*1024*1024, byteSize/(1024*1024*1024.f));
        file.resize(byteSize);
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<half> BGR = mcast<half>(map);

        Time time {true};
        log("Surface parametrized render");
        time.start();
        uint64 totalTime[threadCount()], innerTime[threadCount()];
        parallel_chunk(0, scene.faces.size, [&scene, detailCellCount, &folder, BGR, &totalTime, &innerTime](uint unused id, uint start, uint sizeI) {
            tsc totalTSC, innerTSC;
            totalTSC.start();
            for(const size_t faceIndex: range(start, start+sizeI)) {
                const vec3 A (scene.X[0][faceIndex], scene.Y[0][faceIndex], scene.Z[0][faceIndex]);
                const vec3 B (scene.X[1][faceIndex], scene.Y[1][faceIndex], scene.Z[1][faceIndex]);
                const vec3 C (scene.X[2][faceIndex], scene.Y[2][faceIndex], scene.Z[2][faceIndex]);
                const vec3 D (scene.X[3][faceIndex], scene.Y[3][faceIndex], scene.Z[3][faceIndex]);

                const vec3 faceCenter = (A+B+C+D)/4.f;
                const vec3 N = normalize(cross(C-A, B-A));
                // Viewpoint st with maximum projection
                vec2 st = clamp(vec2(-1),
                                scene.scale*(faceCenter.xy()-scene.viewpoint.xy()) + (scene.scale*(faceCenter.z-scene.viewpoint.z)/(N.z==0?0/*-0 negates infinities*/:-N.z))*N.xy(),
                                vec2(1));
                if(!N.z) {
                    if(!N.x) st.x = 0;
                    if(!N.y) st.y = 0;
                }
                // Projects vertices along st view rays on uv plane (perspective)
                mat4 M = shearedPerspective(st[0], st[1], scene.near, scene.far);
                M.scale(scene.scale); // Fits scene within -1, 1
                M.translate(-scene.viewpoint);
                const vec2 uvA = (M*A).xy();
                const vec2 uvB = (M*B).xy();
                const vec2 uvC = (M*C).xy();
                const vec2 uvD = (M*D).xy();
                const float maxU = ::max(length(uvB-uvA), length(uvC-uvD)); // Maximum projected edge length along quad's u axis
                const float maxV = ::max(length(uvD-uvA), length(uvC-uvB)); // Maximum projected edge length along quad's v axis

                Scene::Face& face = scene.faces[faceIndex];
                const float cellCount = face.attributes.reflect ? detailCellCount : 1;
                const uint U = align(2, ceil(maxU*cellCount)), V = align(2, ceil(maxV*cellCount)); // Aligns UV to 2 for correct 32bit gather indexing
                assert_(U && V);

                // Allocates (s,t) (u,v) images
                half* const faceBGR = BGR.begin()+ (size_t)face.attributes.BGR; // base + index

                const vec3 ab = B-A;
                const vec3 ad = D-A;
                const vec3 badc = A-B+C-D;

                // Shades surface
                const size_t faceSampleCount = U*V;
                const size_t size = stSize*faceSampleCount;
                for(uint svIndex: range(V)) for(uint suIndex: range(U)) {
                    const float v = (float(svIndex)+1.f/2)/float(V);
                    const float u = (float(suIndex)+1.f/2)/float(U);
                    const vec3 P = A + ad*v + (ab + badc*v) * u;
                    const size_t uvIndex = svIndex*U+suIndex;
                    if(!face.attributes.reflect) {
                        for(uint t: range(tSize)) for(uint s: range(sSize)) {
                            const size_t base = uvIndex+(t*sSize+s)*faceSampleCount;
                            faceBGR[0*size+base] = face.attributes.color.b;
                            faceBGR[1*size+base] = face.attributes.color.g;
                            faceBGR[2*size+base] = face.attributes.color.r;
                        }
                    } else {
                        innerTSC.start();
#if 0
                        for(uint t: range(tSize)) for(uint s: range(sSize)) {
                            bgr3f color;
                            const vec3 viewpoint = scene.viewpoint + vec3((s/float(sSize-1))*2-1, (t/float(tSize-1))*2-1, 0)/scene.scale;
                            const vec3 D = (P-viewpoint);
                            const vec3 R = (D - 2*dot(N, D)*N);
                            innerTSC.start();
                            bgr3f reflected = scene.raycast(P, R);
                            innerTSC.stop();
                            color = bgr3f(reflected.b, reflected.g/2, reflected.r/2);
                            const size_t index = (t*sSize+s)*faceSampleCount+uvIndex;
                            faceBGR[0*size+index] = color.b;
                            faceBGR[1*size+index] = color.g;
                            faceBGR[2*size+index] = color.r;
                        }
#else
                        const float iScale = 1./scene.scale;
                        const float Dz = P.z-scene.viewpoint.z;
                        for(uint t: range(tSize)) {
                            const float Dy = P.y - (scene.viewpoint.y + iScale * (t/((tSize-1)/2.f)-1)); // FIXME: Dy = at+b
                            const float NzDzNyDy = N.z*Dz + N.y*Dy;
                            static constexpr v8si seqI = v8si{0,1,2,3,4,5,6,7};
                            for(uint s=0; s<sSize; s += 8) {
                                const v8sf Dx = float8(P.x)-float8(scene.viewpoint.x) + float8(iScale) * (toFloat(s+seqI)/float8((sSize-1)/2.f)-_1f);
                                const v8sf dotND = float8(NzDzNyDy) + float8(N.x)*Dx;
                                const v8sf Rx = Dx - 2*dotND*float8(N.x);
                                const v8sf Ry = Dy - 2*dotND*float8(N.y);
                                const v8sf Rz = Dz - 2*dotND*float8(N.z);
                                // FIXME: R = Ls (factorized linear application)
                                v8si index = scene.raycast(float8(P.x),float8(P.y),float8(P.z), Rx,Ry,Rz);
                                // FIXME: gather SoA face.color
                                const size_t base = uvIndex+(t*sSize+s)*faceSampleCount;
                                for(int k: range(8)) {
                                    const bgr3f reflected = index[k] == -1 ? bgr3f(0) : scene.faces[index[k]].attributes.color;
                                    const bgr3f color = bgr3f(reflected.b, reflected.g/2, reflected.r/2);
                                    // FIXME: uv st (store compact s without scatter, same locality as gather is over stuv (4D bilinear interpolation))
                                    faceBGR[0*size+base+k*faceSampleCount] = color.b;
                                    faceBGR[1*size+base+k*faceSampleCount] = color.g;
                                    faceBGR[2*size+base+k*faceSampleCount] = color.r;
                                }
                            }
                        }
                        innerTSC.stop();
#endif
                    }
                }
#if 1 // DEBUG
                Image bgr (sSize*U, tSize*V);
                extern uint8 sRGB_forward[0x1000];
                for(uint t: range(tSize)) for(uint s: range(sSize)) for(uint svIndex: range(V)) for(uint suIndex: range(U)) {
                    const size_t uvIndex = svIndex*U+suIndex;
                    const size_t index = (t*sSize+s)*faceSampleCount+uvIndex;
                    bgr(s*U+suIndex, t*V+svIndex) = byte4(
                            sRGB_forward[uint(faceBGR[0*size+index]*0xFFF)],
                            sRGB_forward[uint(faceBGR[1*size+index]*0xFFF)],
                            sRGB_forward[uint(faceBGR[2*size+index]*0xFFF)], 0xFF);
                }
                writeFile(str(faceIndex)+".png", encodePNG(bgr), folder);
#endif
            }
            totalTime[id] = totalTSC;
            innerTime[id] = innerTSC;
        });
        log("Rendered in", time, strD(sum(ref<uint64>(innerTime, threadCount())),sum(ref<uint64>(totalTime, threadCount()))));

#else // Dual plane render
        const size_t N = 33;
        const uint2 size = 1024;

        File file(str(N)+'x'+str(N)+'x'+strx(size), folder, Flags(ReadWrite|Create));
        size_t byteSize = 4*N*N*size.y*size.x*sizeof(half);
        assert_(byteSize <= 16ull*1024*1024*1024);
        file.resize(byteSize);
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<half> field = mcast<half>(map);
        profile( field.clear() ); // Explicitly clears to avoid performance skew from clear on page faults

        buffer<Scene::Renderer<Scene::RaycastShader, 3>> renderers (threadCount());
        for(auto& renderer: renderers) new (&renderer) Scene::Renderer<Scene::RaycastShader, 4>(scene);

        // Fits scene
        vec3 min = inff, max = -inff;
        for(const Scene::Face& f: scene.faces) for(vec3 p: f.position) { min = ::min(min, p); max = ::max(max, p); }
        max.z += 0x1p-8; // Prevents back and far plane from Z-fighting
        const float scale = 2./::max(max.x-min.x, max.y-min.y);
        const float near = scale*(-scene.viewpoint.z+min.z);
        const float far = scale*(-scene.viewpoint.z+max.z);

        Time time (true);
        parallel_for(0, N*N, [near, far, scale, &scene, field, size, &renderers, &folder](uint threadID, size_t stIndex) {
            int sIndex = stIndex%N, tIndex = stIndex/N;

            // Sheared perspective (rectification)
            const float s = sIndex/float(N-1), t = tIndex/float(N-1);
            mat4 M = shearedPerspective(s*2-1, t*2-1, near, far);
            M.scale(scale); // Fits scene within -1, 1
            M.translate(-scene.viewpoint);

            ImageH Z (unsafeRef(field.slice(((0ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            ImageH B (unsafeRef(field.slice(((1ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            ImageH G (unsafeRef(field.slice(((2ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            ImageH R (unsafeRef(field.slice(((3ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);

            auto& renderer = renderers[threadID];
            renderer.shader.viewpoint = scene.viewpoint + vec3(s*2-1,t*2-1,0)/scale;
            scene.render(renderer, M, (float[]){1,1,1}, Z, B, G, R);

            ImageH Z01 (Z.size);
            for(size_t i: range(Z.ref::size)) Z01[i] = (Z[i]+1)/2;
            if(sIndex%16==0 && tIndex%16==0) writeFile(str(sIndex)+'_'+str(tIndex)+".Z.png", encodePNG(convert(Z01, Z01, Z01)), folder, true);
            if(sIndex%16==0 && tIndex%16==0) writeFile(str(sIndex)+'_'+str(tIndex)+".BGR.png", encodePNG(convert(B, G, R)), folder, true);
        });
        log("Rendered",strx(uint2(N)),"x",strx(size),"images in", time);
#endif
    }
} render;
