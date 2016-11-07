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
        const float cellCount = 32;

        // Fits scene
        vec3 min = inff, max = -inff;
        for(const Scene::Face& f: scene.faces) for(vec3 p: f.position) { min = ::min(min, p); max = ::max(max, p); }
        max.z += 0x1p-8; // Prevents back and far plane from Z-fighting
        const float scale = 2./::max(max.x-min.x, max.y-min.y);
        const float near = scale*(-scene.viewpoint.z+min.z);
        const float far = scale*(-scene.viewpoint.z+max.z);

        // Fits face UV to maximum projected sample rate
        Time time {true};
        log("Surface parametrized render");
        time.start();
        parallel_chunk(0, scene.faces.size, [&scene, scale, near, far, cellCount](uint unused id, uint start, uint sizeI) {
            for(const uint faceIndex: range(start, start+sizeI)) {
                Scene::Face& face = scene.faces[faceIndex];
                const vec3 a = face.position[0], b = face.position[1], c = face.position[2], d = face.position[3];
                const vec3 faceCenter = (a+b+c+d)/4.f;
                const vec3 N = cross(c-a, b-a);
                // Viewpoint st with maximum projection
                vec2 st = clamp(vec2(-1),
                                scale*(faceCenter.xy()-scene.viewpoint.xy()) + (scale*(faceCenter.z-scene.viewpoint.z)/(N.z==0?0/*-0 negates infinities*/:-N.z))*N.xy(),
                                vec2(1));
                if(!N.z) {
                    if(!N.x) st.x = 0;
                    if(!N.y) st.y = 0;
                }
                // Projects vertices along st view rays on uv plane (perspective)
                // FIXME
                /*const vec2 uvA = st + scale*(a.z-scene.viewpoint.z)/near * (scale*(a.xy()-scene.viewpoint.xy())-st);
                      const vec2 uvB = st + scale*(b.z-scene.viewpoint.z)/near * (scale*(b.xy()-scene.viewpoint.xy())-st);
                      const vec2 uvC = st + scale*(c.z-scene.viewpoint.z)/near * (scale*(c.xy()-scene.viewpoint.xy())-st);
                      const vec2 uvD = st + scale*(d.z-scene.viewpoint.z)/near * (scale*(d.xy()-scene.viewpoint.xy())-st);*/
                mat4 M = shearedPerspective(st[0], st[1], near, far);
                M.scale(scale); // Fits scene within -1, 1
                M.translate(-scene.viewpoint);
                const vec2 uvA = (M*a).xy();
                const vec2 uvB = (M*b).xy();
                const vec2 uvC = (M*c).xy();
                const vec2 uvD = (M*d).xy();
                const float maxU = ::max(length(uvB-uvA), length(uvC-uvD)); // Maximum projected edge length along quad's u axis
                const float maxV = ::max(length(uvD-uvA), length(uvC-uvB)); // Maximum projected edge length along quad's v axis
                const uint U = ceil(maxU*cellCount), V = ceil(maxV*cellCount);
                assert_(U && V);
                // Scales uv for texture sampling (unnormalized)
                for(float& u: face.u) { u *= U; assert_(isNumber(u)); }
                for(float& v: face.v) { v *= V; assert_(isNumber(v)); }
                // Allocates image (FIXME)
                face.B = Image8(U, V);
                face.G = Image8(U, V);
                face.R = Image8(U, V);

                const vec3 ab = b-a;
                const vec3 ad = d-a;
                const vec3 badc = a-b+c-d;

                // Shades surface
                for(uint svIndex: range(V)) for(uint suIndex: range(U)) {
                    const float v = (float(svIndex)+1.f/2)/float(V);
                    const float u = (float(suIndex)+1.f/2)/float(U);
                    const vec3 P = a + ad*v + (ab + badc*v) * u;
                    bgr3f color;
                    if(!face.reflect) color=face.color;
                    else {
                        const vec3 viewpoint = scene.viewpoint;
                        const vec3 D = normalize(P-viewpoint);
                        const vec3 R = D - 2*dot(N, D)*N;
                        bgr3f reflected = Scene::raycast(scene.faces, P, normalize(R));
                        color = bgr3f(reflected.b, reflected.g/2, reflected.r/2);
                    }
                    const size_t index = svIndex*U+suIndex;
                    face.B[index] = 0xFF*color.b;
                    face.G[index] = 0xFF*color.g;
                    face.R[index] = 0xFF*color.r;
                    // TODO: View dependent representation
                }
            }
        });
        size_t sampleCount = 0;
        for(const Scene::Face& face: scene.faces) sampleCount += face.B.ref::size;
        log(sampleCount, "samples (per component)");

        assert_(uint(cellCount) == cellCount);
        File file(str(uint(cellCount)), folder, Flags(ReadWrite|Create));
        size_t byteSize = 3*sampleCount;
        assert_(byteSize <= 12ull*1024*1024*1024);
        file.resize(byteSize);
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<uint8> BGR = mcast<uint8>(map);
        mref<uint8> B = BGR.slice(0*sampleCount, sampleCount);
        mref<uint8> G = BGR.slice(1*sampleCount, sampleCount);
        mref<uint8> R = BGR.slice(2*sampleCount, sampleCount);
        size_t index = 0;
        for(const Scene::Face& face: scene.faces) { // FIXME: direct no copy storage
            B.slice(index,face.B.ref::size).copy(face.B);
            G.slice(index,face.G.ref::size).copy(face.G);
            R.slice(index,face.R.ref::size).copy(face.R);
            index += face.B.ref::size;
        }
        assert_(index == sampleCount);

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
