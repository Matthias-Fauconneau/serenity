#include "scene.h"
#include "parallel.h"
#include "png.h"
#include "interface.h"
#include "text.h"
#include "render.h"
#include "window.h"

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

    ImageH sumB, sumG, sumR;
    size_t count = 0;
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
        window->actions[Key('s')] = [this]{ displaySurfaceParametrized=!displaySurfaceParametrized; window->render(); };
        window->actions[Key('p')] = [this]{ displayParametrization=!displayParametrization; window->render(); };
        window->actions[Key('`')] = [this]{ load(1<<0); window->render(); };
        window->actions[Key('0')] = [this]{ load(1<<0); window->render(); };
        window->actions[Key('1')] = [this]{ load(1<<1); window->render(); };
        window->actions[Key('2')] = [this]{ load(1<<2); window->render(); };
        window->actions[Key('3')] = [this]{ load(1<<3); window->render(); };
        window->actions[Key('4')] = [this]{ load(1<<4); window->render(); };
        window->actions[Key('5')] = [this]{ load(1<<5); window->render(); };
        window->setTitle(strx(uint2(sSize,tSize)));
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

            const ref<half> BGR = cast<half>(surfaceMap);

            size_t index = 0; size_t lastU = 0;
            for(size_t faceIndex: range(scene.faces.size)) {
                const vec3 A (scene.X[0][faceIndex], scene.Y[0][faceIndex], scene.Z[0][faceIndex]);
                const vec3 B (scene.X[1][faceIndex], scene.Y[1][faceIndex], scene.Z[1][faceIndex]);
                const vec3 C (scene.X[2][faceIndex], scene.Y[2][faceIndex], scene.Z[2][faceIndex]);
                const vec3 D (scene.X[3][faceIndex], scene.Y[3][faceIndex], scene.Z[3][faceIndex]);

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

                Scene::Face& face = scene.faces[faceIndex];
                const float cellCount = detailCellCount; //face.reflect ? detailCellCount : 1;
                const uint U = align(2, ceil(maxU*cellCount)), V = align(2, ceil(maxV*cellCount)); // Aligns UV to 2 for correct 32bit gather indexing
                assert_(U && V);

                // Scales uv for texture sampling (unnormalized)
                for(float& u: face.u) { u = u ? U-1 : 0; assert_(isNumber(u)); }
                for(float& v: face.v) { v = v ? V-1 : 0; assert_(isNumber(v)); }

                // No copy (surface samples needs to stay memory mapped)
                face.BGR = BGR.data+index;
                face.size.x = U;
                face.size.y = V;

                index += 3*V*U*tSize*sSize;
                if(faceIndex == scene.faces.size-1) lastU = U; // Prevents OOB on interpolation
            }

            if(scale>1 && (sSize==sMaxSize||tSize==tMaxSize)) {
                Time time{true};
                log(strx(uint2(sSize,tSize)), "->", strx(uint2(sSize,tSize)/scale));
                File file(str(uint(detailCellCount))+'x'+strx(uint2(sSize,tSize)/scale), folder, Flags(ReadWrite|Create));
                assert_(index%sq(scale)==0);
                size_t byteSize = (index/sq(scale)+lastU/scale)*sizeof(half);
                file.resize(byteSize);
                Map subsampleMap (file, Map::Prot(Map::Read|Map::Write));
                const mref<half> target = mcast<half>(subsampleMap);

                size_t sourceIndex = 0;
                for(size_t i : range(scene.faces.size)) {
                    Scene::Face& face = scene.faces[i];
                    const uint U = face.size.x, V = face.size.y;
                    const half* const faceSource = face.BGR;
                    half* const faceTarget = target.begin()+sourceIndex/sq(scale);
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
                log(time);
                load(scale); // Loads new file
                log(time);
            }
#if 0 // DEBUG
            log("DEBUG");
                for(const size_t faceIndex: range(scene.faces.size)) {
                    const Scene::Face& face = scene.faces[faceIndex];
                    const uint U = face.size.x, V = face.size.y;
                    const uint VU = V*U;
                    const uint size4 = tSize*sSize*VU;
                    const half* const faceBGR = face.BGR; // base + index
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

#if 0 // Raycast (FIXME: sheared)
        vec3 O = scene.viewpoint + vec3(s,t,0)/scene.scale;
        parallel_chunk(target.size.y, [this, &target, O](uint, size_t start, size_t sizeI) {
            const int targetSizeX = target.size.x;
            for(size_t targetY: range(start, start+sizeI)) for(size_t targetX: range(targetSizeX)) {
                size_t targetIndex = targetY*targetSizeX+targetX;
                const vec2 uv = (vec2(targetX, targetY) / vec2(target.size-uint2(1)))*2.f - vec2(1);
                const vec3 d = normalize(vec3(uv, scene.near));
                v8si index = scene.raycast(float8(O.x), float8(O.y), float8(O.z), float8(d.x), float8(d.y), float8(d.z));
                bgr3f S = scene.faces[index[0]].color;
                extern uint8 sRGB_forward[0x1000];
                target[targetIndex] = byte4(sRGB_forward[uint(S.b*0xFFF)], sRGB_forward[uint(S.g*0xFFF)], sRGB_forward[uint(S.r*0xFFF)], 0xFF);
            }
        });
#else
        ImageH B (target.size), G (target.size), R (target.size);
        if(displayParametrization || (displaySurfaceParametrized && sSize && tSize)) {
            if(displayParametrization)
                scene.render(UVRenderer, M, (float[]){1,1,1}, {}, B, G, R);
            else if(displaySurfaceParametrized && sSize && tSize) {
                TexRenderer.shader.setFaceAttributes(scene.faces, sSize, tSize, (s+1)/2, (t+1)/2);
                scene.render(TexRenderer, M, (float[]){1,1,1}, {}, B, G, R);
            }
            assert_(target.size == B.size);
            extern uint8 sRGB_forward[0x1000];
            for(size_t i: range(target.ref::size)) {
                uint b = uint(B[i]*0xFFF);
                uint g = uint(G[i]*0xFFF);
                uint r = uint(R[i]*0xFFF);
                assert_(b >= 0 && b <= 0xFFF, b);
                assert_(g >= 0 && g <= 0xFFF, g);
                assert_(r >= 0 && r <= 0xFFF, r);
                target[i] = byte4(sRGB_forward[b], sRGB_forward[g], sRGB_forward[r], 0xFF);
            }
        } else {
            BGRRenderer.shader.viewpoint = vec3(s,t,0)/scene.scale;
            scene.render(BGRRenderer, M, (float[]){1,1,1}, {}, B, G, R);
            if(view.viewYawPitch != viewYawPitch || sumB.size != target.size) { // Resets accumulation
                if(sumB.size != target.size) {
                    sumB = ImageH(target.size);
                    sumG = ImageH(target.size);
                    sumR = ImageH(target.size);
                }
                viewYawPitch = view.viewYawPitch;
                for(size_t i: range(B.ref::size)) sumB[i] = B[i];
                for(size_t i: range(G.ref::size)) sumG[i] = G[i];
                for(size_t i: range(R.ref::size)) sumR[i] = R[i];
                count = 1;
            } else {
                for(size_t i: range(B.ref::size)) sumB[i] += B[i];
                for(size_t i: range(G.ref::size)) sumG[i] += G[i];
                for(size_t i: range(R.ref::size)) sumR[i] += R[i];
                count++;
            }
            assert_(target.size == B.size);
            extern uint8 sRGB_forward[0x1000];
            for(size_t i: range(target.ref::size)) {
                uint B = uint(sumB[i]/count*0xFFF);
                uint G = uint(sumG[i]/count*0xFFF);
                uint R = uint(sumR[i]/count*0xFFF);
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
#endif
        return target;
    }
} view;
