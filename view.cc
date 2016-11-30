#define RT 1
#include "scene.h"
#include "parallel.h"
#include "png.h"
#include "interface.h"
#include "text.h"
#include "render.h"
#include "window.h"
#include "render.h"

struct ViewControl : virtual Widget {
    vec2 viewYawPitch = vec2(0, 0); // Current view angles

    struct {
        vec2 cursor;
        vec2 viewYawPitch;
    } dragStart {0, 0};

    // Orbital ("turntable") view control
    virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
        if(event == Press) {
            dragStart = {cursor, viewYawPitch};
            return true;
        }
        if(event==Motion && button==LeftButton) {
            viewYawPitch = dragStart.viewYawPitch + float(2*PI) * (cursor - dragStart.cursor) / size;
            viewYawPitch.x = clamp<float>(-PI/3, viewYawPitch.x, PI/3);
            viewYawPitch.y = clamp<float>(-PI/3, viewYawPitch.y, PI/3);
        }
        else return false;
        return true;
    }
};

struct ViewApp {
    Render renderer;
    Folder folder {basename(arguments()[0]),"/var/tmp"_,true};
    const uint2 imageSize = 1024;
    Map surfaceMap; // Needs to stay memory mapped for face B,G,R references
    uint sSize = 0, tSize = 0;

    Scene scene {::parseScene(readFile(sceneFile(basename(arguments()[0]))))};
    Scene::Renderer<Scene::NoShader> Zrenderer {};
    Scene::Renderer<Scene::TextureShader> TexRenderer {scene};
    Scene::Renderer<Scene::CheckerboardShader> UVRenderer {};
    Scene::Renderer<Scene::RaycastShader> BGRRenderer {scene};

    bool displayField = false; // or rasterize geometry
    bool depthCorrect = true; // when displaying field
    bool displaySurfaceParametrized = false; // baked surface parametrized appearance or direct renderer (raycast shader) (when rasterizing)
    bool displayParametrization = false; // or checkerboard pattern (when rasterizing)

    ImageF sumB[2], sumG[2], sumR[2];
    uint count[2] = {0, 0}; // Iteration count (Resets on view change)
    uint randomCount = 0; // To regenerate new lookup
    vec2 viewYawPitch;

    struct ViewWidget : ViewControl, ImageView {
        ViewApp& _this;
        ViewWidget(ViewApp& _this) : _this(_this) {}

        virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& widget) override {
            return ViewControl::mouseEvent(cursor,size,event,button,widget);
        }
        virtual vec2 sizeHint(vec2) override { return vec2(_this.imageSize); }
        virtual shared<Graphics> graphics(vec2 size) override {
            this->image = _this.render(uint2(size));
            return ImageView::graphics(size);
        }
    } view {*this};
    unique<Window> window = nullptr;

    ViewApp() {
        window = ::window(&view);
        load(1);
        window->actions[Key('f')] = [this]{ displayField=!displayField; window->render(); };
        window->actions[Key('d')] = [this]{ depthCorrect=!depthCorrect; window->render(); };
        window->actions[Key('b')] = [this]{ displaySurfaceParametrized=!displaySurfaceParametrized; window->render(); };
        window->actions[Key('p')] = [this]{ displayParametrization=!displayParametrization; window->render(); };
        window->actions[Key('r')] = [this]{ scene.rasterize=!scene.rasterize; window->render(); };
        window->actions[Key('i')] = [this]{ scene.indirect=!scene.indirect; renderer.scene.indirect=scene.indirect?8:0; renderer.clear(); count[0]=count[1]=0; window->render(); };
        window->actions[Key('s')] = [this]{ scene.specular=!scene.specular; renderer.scene.specular=scene.specular; renderer.clear(); count[0]=count[1]=0; window->render(); };
        window->actions[Key('`')] = [this]{ load(1<<0); window->render(); };
        window->actions[Key('0')] = [this]{ load(1<<0); window->render(); };
        window->actions[Key('1')] = [this]{ load(1<<1); window->render(); };
        window->actions[Key('2')] = [this]{ load(1<<2); window->render(); };
        window->actions[Key('3')] = [this]{ load(1<<3); window->render(); };
        window->actions[Key('4')] = [this]{ load(1<<4); window->render(); };
        window->actions[Key('5')] = [this]{ load(1<<5); window->render(); };
        window->setTitle(strx(uint2(sSize,tSize)));
        renderer.clear();
        {Random random; for(Lookup& lookup: scene.lookups) lookup.generate(random);} // First set of stratified cosine samples for hemispheric rasterizer
        TexRenderer.shader.setFaceAttributes(scene.faces, sSize, tSize, 0.5, 0.5); // FIXME: TODO: store diffuse (average s,t) texture for non-primary (diffuse) evaluation
        scene.textureShader = &TexRenderer.shader; // FIXME
        renderer.scene.textureShader = &TexRenderer.shader; // FIXME
    }
    void load(const uint scale = 1) {
        surfaceMap = {};
        sSize = 0, tSize = 0;
        for(Scene::Face& face : scene.faces) face.BGR = 0;

        mat4 NDC;
        NDC.scale(vec3(vec2(imageSize-uint2(1))/2.f, 1)); // 0, 2 -> pixel size (resolved)
        NDC.translate(vec3(vec2(1), 0.f)); // -1, 1 -> 0, 2

        mat4 M0 = shearedPerspective(-1, -1, scene.near, scene.far);
        M0.scale(scene.scale); // Fits scene within -1, 1
        M0 = NDC * M0;

        mat4 M1 = shearedPerspective(1, 1, scene.near, scene.far);
        M1.scale(scene.scale); // Fits scene within -1, 1
        M1 = NDC * M1;

        // Fits face UV to maximum projected sample rate
        float detailCellCount = 0; uint sMaxSize = 0, tMaxSize = 0;
        for(string name: folder.list(Files)) {
            TextData s (name);
            const uint cellCount_ = s.integer(false);
            if(!s.match('x')) continue;
            const uint sSize_ = s.integer(false);
            if(!s.match('x')) continue;
            const uint tSize_ = s.integer(false);
            if(s) continue;
            if(sSize_ < sSize) continue;
            if(tSize_ < tSize) continue;
            if(cellCount_ < detailCellCount) continue;
            detailCellCount = cellCount_;
            sMaxSize = sSize_;
            tMaxSize = tSize_;
        }

        sSize = sMaxSize;
        tSize = tMaxSize;
        if(scale>1 && existsFile(str(uint(detailCellCount))+'x'+strx(uint2(sSize,tSize)/scale), folder)) {
            sSize /= scale;
            tSize /= scale;
        }

        if(detailCellCount && sSize && tSize) {
            surfaceMap = Map(str(uint(detailCellCount))+'x'+str(sSize)+'x'+str(tSize), folder);

            const ref<Float> BGR = cast<Float>(surfaceMap);

            size_t index = 0; size_t lastU = 0;
            for(const size_t faceIndex: range(scene.faces.size/2)) { // FIXME: Assumes quads (TODO: generic triangle UV mapping)
                const vec3 A (scene.X[0][2*faceIndex+0], scene.Y[0][2*faceIndex+0], scene.Z[0][2*faceIndex+0]);
                const vec3 B (scene.X[1][2*faceIndex+0], scene.Y[1][2*faceIndex+0], scene.Z[1][2*faceIndex+0]);
                const vec3 C (scene.X[2][2*faceIndex+0], scene.Y[2][2*faceIndex+0], scene.Z[2][2*faceIndex+0]);
                const vec3 D (scene.X[2][2*faceIndex+1], scene.Y[2][2*faceIndex+1], scene.Z[2][2*faceIndex+1]);

                const vec3 O = (A+B+C+D)/4.f;
                const vec3 N = cross(C-A, B-A);
                // Viewpoint st with maximum projection
                vec2 st = clamp(vec2(-1), scene.scale*O.xy() + (scene.scale*O.z/(N.z==0?0/*-0 negates infinities*/:-N.z))*N.xy(), vec2(1));
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
                assert_(U && V);

                // Scales uv for texture sampling (unnormalized)
                for(float& u: face.u) { u = u ? U-1 : 0; assert_(isNumber(u)); }
                for(float& v: face.v) { v = v ? V-1 : 0; assert_(isNumber(v)); }
                for(float& u: scene.faces[2*faceIndex+1].u) { u = u ? U-1 : 0; assert_(isNumber(u)); }
                for(float& v: scene.faces[2*faceIndex+1].v) { v = v ? V-1 : 0; assert_(isNumber(v)); }

                // No copy (surface samples needs to stay memory mapped)
                face.BGR = BGR.data+index;
                face.size.x = U;
                face.size.y = V;

                // Triangle ACD
                scene.faces[2*faceIndex+1].BGR = BGR.data+index;
                scene.faces[2*faceIndex+1].size.x = U;
                scene.faces[2*faceIndex+1].size.y = V;

                index += 3*V*U*tSize*sSize;
                if(faceIndex+1 == scene.faces.size-1) lastU = U; // Prevents OOB on interpolation
            }

            if(scale>1 && (sSize==sMaxSize||tSize==tMaxSize)) {
                File file(str(uint(detailCellCount))+'x'+strx(uint2(sSize,tSize)/scale), folder, Flags(ReadWrite|Create));
                assert_(index%sq(scale)==0);
                size_t byteSize = (index/sq(scale)+lastU/scale)*sizeof(Float);
                file.resize(byteSize);
                Map subsampleMap (file, Map::Prot(Map::Read|Map::Write));
                const mref<Float> target = mcast<Float>(subsampleMap);

                size_t sourceIndex = 0;
                for(size_t faceIndex : range(scene.faces.size/2)) {
                    Scene::Face& face = scene.faces[faceIndex*2];
                    const uint U = face.size.x, V = face.size.y;
                    const Float* const faceSource = face.BGR;
                    Float* const faceTarget = target.begin()+sourceIndex/sq(scale);
                    for(size_t c: range(3)) {
                        for(size_t v: range(V)) for(size_t u: range(U)) {
                            for(size_t t: range(tSize/scale)) for(size_t s: range(sSize/scale)) {
                                float sum = 0;
                                for(uint dt: range(scale)) {
                                    for(uint ds: range(scale)) {
                                        sum += faceSource[(c*tSize*sSize + (t*scale+dt)*sSize + (s*scale+ds))*V*U + v*U + u];
                                    }
                                }
                                faceTarget[(c*(tSize/scale)*(sSize/scale) + t*(sSize/scale) + s)*V*U + v*U + u] = sum / sq(scale);
                            }
                        }
                    }
                    sourceIndex += 3*V*U*tSize*sSize;
                }
                load(scale); // Loads new file
            }
#if 0 // DEBUG
            log("DEBUG");
                for(const size_t faceIndex: range(scene.faces.size/2)) {
                    const Scene::Face& face = scene.faces[faceIndex*2];
                    const uint U = face.size.x, V = face.size.y;
                    const uint VU = V*U;
                    const uint size4 = tSize*sSize*VU;
                    const Float* const faceBGR = face.BGR; // base + index
                    Image bgr (sSize*U, tSize*V);
                    extern uint8 sRGB_forward[0x1000];
                    for(uint svIndex: range(V)) for(uint suIndex: range(U)) for(uint t: range(tSize)) for(uint s: range(sSize)) {
                        const uint index = (sSize * t + s)*VU + (svIndex*U+suIndex);
                        bgr(s*U+suIndex, t*V+svIndex) = byte4(
                                    sRGB_forward[uint(faceBGR[0*size4+index]*0xFFF)],
                                    sRGB_forward[uint(faceBGR[1*size4+index]*0xFFF)],
                                    sRGB_forward[uint(faceBGR[2*size4+index]*0xFFF)], 0xFF);
                    }
                    writeFile(str(faceIndex)+".png", encodePNG(bgr), folder, true);
                    //break;
                }
#endif
        } //else error(sSize, tSize);
        if(window) window->setTitle(strx(uint2(sSize,tSize)));
    }
    Image render(uint2 targetSize) {
        Image target (targetSize);

        // Sheared perspective (rectification)
        const float s = view.viewYawPitch.x/(PI/3), t = view.viewYawPitch.y/(PI/3);
        mat4 M = shearedPerspective(s, t, scene.near, scene.far);
        M.scale(scene.scale); // Fits scene within -1, 1

        if(displayParametrization || (displaySurfaceParametrized && sSize && tSize)) {
            ImageH B (target.size), G (target.size), R (target.size);
            if(displayParametrization)
                scene.render(UVRenderer, M, (float[]){1,1,1}, {}, B, G, R);
            else if(displaySurfaceParametrized && sSize && tSize) {
                renderer.step(); // FIXME: async
                renderer.scene.texCount = renderer.iterations; // Enables radiosity based indirect illumination only after first step of baking
                TexRenderer.shader.setFaceAttributes(scene.faces, sSize, tSize, (s+1)/2, (t+1)/2);
                scene.render(TexRenderer, M, (float[]){1,1,1}, {}, B, G, R);
                window->render(); // Accumulates
            }
            assert_(target.size == B.size);
            extern uint8 sRGB_forward[0x1000];
            const float scale = float(0xFFF) / (displaySurfaceParametrized && sSize && tSize ? float(renderer.iterations) : 1.f);
            for(size_t i: range(target.ref::size)) {
                uint b = uint(scale*B[i]);
                uint g = uint(scale*G[i]);
                uint r = uint(scale*R[i]);
                b = clamp(0u, b, 0xFFFu);
                g = clamp(0u, g, 0xFFFu);
                r = clamp(0u, r, 0xFFFu);
                assert_(b >= 0 && b <= 0xFFF, b);
                assert_(g >= 0 && g <= 0xFFF, g);
                assert_(r >= 0 && r <= 0xFFF, r);
                target[i] = byte4(sRGB_forward[b], sRGB_forward[g], sRGB_forward[r], 0xFF);
            }
        } else {
            if(view.viewYawPitch != viewYawPitch || sumB[scene.rasterize].size != target.size) {
                viewYawPitch = view.viewYawPitch;
                for(uint& v: count) v = 0; // Resets accumulation
            }
            if(count[scene.rasterize] == 0) {
                if(sumB[scene.rasterize].size != target.size) {
                    sumB[scene.rasterize] = ImageF(target.size);
                    sumG[scene.rasterize] = ImageF(target.size);
                    sumR[scene.rasterize] = ImageF(target.size);
                }
                sumB[scene.rasterize].clear(0);
                sumG[scene.rasterize].clear(0);
                sumR[scene.rasterize].clear(0);
            }
            count[scene.rasterize]++;
#if 1 // Raycast (FIXME: sheared)
        const vec3 O = vec3(s,t,0)/scene.scale;
        Random randoms[threadCount()];
        for(Random& random: mref<Random>(randoms,threadCount())) random=Random();
        //Time preTime{true};
        if(scene.rasterize && count[scene.rasterize]>1 /*Reset only after first first iterations (reuse previous set on new view)*/) {
            /*if(randomCount%16==0)*/ {Random random; for(Lookup& lookup: scene.lookups) lookup.generate(random);} // New set of stratified cosine samples for hemispheric rasterizer
            //if(count[1] < randomCount) randomCount = 0; // Do not reset while view changes
            //randomCount++;
        }
        //preTime.stop();
        //Time renderTime{true};
        parallel_chunk(target.size.y, [this, &target, O, &randoms](const uint id, const size_t start, const size_t sizeI) {
            const int targetSizeX = target.size.x;
            for(size_t targetY: range(start, start+sizeI)) for(size_t targetX: range(targetSizeX)) {
                size_t targetIndex = targetY*targetSizeX+targetX;
                const vec2 uv = (vec2(targetX, targetY) / vec2(target.size-uint2(1)))*2.f - vec2(1) - O.xy()*scene.scale;
                const vec3 d = normalize(vec3(uv, scene.near));
                Scene::Path path; Scene::Timers timers;
                bgr3f color = scene.raycast_shade(O, d, randoms[id], 0, path, timers, 1);
                sumB[scene.rasterize][targetIndex] += color.b;
                sumG[scene.rasterize][targetIndex] += color.g;
                sumR[scene.rasterize][targetIndex] += color.r;
            }
        });
        //log(preTime, renderTime, strD(preTime, renderTime));
#else
            BGRRenderer.shader.viewpoint = vec3(s,t,0)/scene.scale;
            scene.render(BGRRenderer, M, (float[]){1,1,1}, {}, B, G, R);
            for(size_t i: range(B.ref::size)) sumB[i] += B[i];
            for(size_t i: range(G.ref::size)) sumG[i] += G[i];
            for(size_t i: range(R.ref::size)) sumR[i] += R[i];
#endif
            extern uint8 sRGB_forward[0x1000];
            for(size_t i: range(target.ref::size)) {
                uint B = uint(sumB[scene.rasterize][i]/count[scene.rasterize]*0xFFF);
                uint G = uint(sumG[scene.rasterize][i]/count[scene.rasterize]*0xFFF);
                uint R = uint(sumR[scene.rasterize][i]/count[scene.rasterize]*0xFFF);
                B = clamp(0u, B, 0xFFFu);
                G = clamp(0u, G, 0xFFFu);
                R = clamp(0u, R, 0xFFFu);
                assert_(B >= 0 && B <= 0xFFF, B);
                assert_(G >= 0 && G <= 0xFFF, G);
                assert_(R >= 0 && R <= 0xFFF, R);
                target[i] = byte4(sRGB_forward[B], sRGB_forward[G], sRGB_forward[R], 0xFF);
            }
            window->render(); // Accumulates
        }
        return target;
    }
} view;
