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
        const float cellCount = 256;
        const uint sSize = 16, tSize = sSize; // Number of view-dependent samples along (s,t) dimensions
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
            const uint U = align(2, ceil(maxU*cellCount)), V = align(2, ceil(maxV*cellCount)); // Aligns UV to 2 for correct 32bit gather indexing
            assert_(U && V);
            // Allocates (s,t) (u,v) images
            Scene::Face& face = scene.faces[i];
            face.attributes.BGR = (half*)sampleCount;
            sampleCount += 3*tSize*sSize*V*U;
        }

        assert_(uint(cellCount) == cellCount);
        File file(str(uint(cellCount))+'x'+str(sSize)+'x'+str(tSize), folder, Flags(ReadWrite|Create));
        size_t byteSize = sampleCount*sizeof(half);
        log(strx(uint2(sSize, tSize)), "=", sampleCount, "samples (per component)", "=", byteSize/1024/1024.f, "M");
        assert_(byteSize <= 16ull*1024*1024*1024, byteSize/(1024*1024*1024.f));
        file.resize(byteSize);
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<half> BGR = mcast<half>(map);

        Time time {true};
        log("Surface parametrized render");
        time.start();
        parallel_chunk(0, scene.faces.size, [&scene, cellCount, &folder, BGR](uint unused id, uint start, uint sizeI) {
            for(const size_t i: range(start, start+sizeI)) {
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
                const uint U = align(2, ceil(maxU*cellCount)), V = align(2, ceil(maxV*cellCount)); // Aligns UV to 2 for correct 32bit gather indexing
                assert_(U && V);

                // Allocates (s,t) (u,v) images
                Scene::Face& face = scene.faces[i];
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
                    for(uint t: range(tSize)) for(uint s: range(sSize)) {
                        bgr3f color;
                        if(!face.attributes.reflect) color = face.attributes.color;
                        else {
                            const vec3 viewpoint = scene.viewpoint + vec3((s/float(sSize-1))*2-1, (t/float(tSize-1))*2-1, 0)/scene.scale;
                            const vec3 D = normalize(P-viewpoint);
                            const vec3 R = D - 2*dot(N, D)*N;
                            bgr3f reflected = scene.raycast(P, normalize(R));
                            color = bgr3f(reflected.b, reflected.g/2, reflected.r/2);
                        }
                        const size_t index = (t*sSize+s)*faceSampleCount+uvIndex;
                        faceBGR[0*size+index] = color.b;
                        faceBGR[1*size+index] = color.g;
                        faceBGR[2*size+index] = color.r;
                    }
                }
#if 0 // DEBUG
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
        });
        log("Rendered in", time);

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
