#include "field.h"
#include "scene.h"
#include "parallel.h"

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

struct LightFieldViewApp : LightField {
    Map surfaceMap; // Needs to stay memory mapped for face B,G,R references
    uint sSize = 0, tSize = 0;

    Scene scene {::parseScene(readFile(sceneFile(basename(arguments()[0]))))};
    Scene::Renderer<Scene::TextureShader, 0> Zrenderer {scene};
    Scene::Renderer<Scene::TextureShader, 3> TexRenderer {scene};
    Scene::Renderer<Scene::CheckerboardShader, 3> UVRenderer {scene};
    Scene::Renderer<Scene::RaycastShader, 3> BGRRenderer {scene};

    bool displayField = false; // or rasterize geometry
    bool depthCorrect = true; // when displaying field
    bool displaySurfaceParametrized = true; // baked surface parametrized appearance or direct renderer (raycast shader) (when rasterizing)
    bool displayParametrization = false; // or checkerboard pattern (when rasterizing)

    struct LightFieldViewWidget : ViewControl, ImageView {
        LightFieldViewApp& _this;
        LightFieldViewWidget(LightFieldViewApp& _this) : _this(_this) {}

        virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& widget) override {
            return ViewControl::mouseEvent(cursor,size,event,button,widget);
        }
        virtual vec2 sizeHint(vec2) override { return vec2(1024); }
        virtual shared<Graphics> graphics(vec2 size) override {
            this->image = _this.render(uint2(size));
            return ImageView::graphics(size);
        }
    } view {*this};
    unique<Window> window = nullptr;

    LightFieldViewApp() : LightField(Folder(basename(arguments()[0])+(arguments().contains("coverage")?"/coverage"_:""_),"/var/tmp"_,true)) {
        // Fits scene
        vec3 min = inff, max = -inff;
        for(const Scene::Face& f: scene.faces) for(vec3 p: f.position) { min = ::min(min, p); max = ::max(max, p); }
        max.z += 0x1p-8; // Prevents back and far plane from Z-fighting
        const float scale = 2./::max(max.x-min.x, max.y-min.y);
        const float near = scale*(-scene.viewpoint.z+min.z);
        const float far = scale*(-scene.viewpoint.z+max.z);

        mat4 NDC;
        NDC.scale(vec3(vec2(imageSize-uint2(1))/2.f, 1)); // 0, 2 -> pixel size (resolved)
        NDC.translate(vec3(vec2(1), 0.f)); // -1, 1 -> 0, 2

        mat4 M0 = shearedPerspective(-1, -1, near, far);
        M0.scale(scale); // Fits scene within -1, 1
        M0.translate(-scene.viewpoint);
        M0 = NDC * M0;

        mat4 M1 = shearedPerspective(1, 1, near, far);
        M1.scale(scale); // Fits scene within -1, 1
        M1.translate(-scene.viewpoint);
        M1 = NDC * M1;

        // Fits face UV to maximum projected sample rate
        Time time {true};
        log("Surface parametrization");
        time.start();
#if 0
        // Visibility estimation from planar viewpoint array
        parallel_chunk(0, scene.faces.size, [this, M0, M1, scale, near, far](uint unused id, uint start, uint sizeI) {
            const uint sSize = imageCount.x, tSize = imageCount.y;
            const float sSizeScale = 1./float(sSize-1), tSizeScale = 1./float(tSize-1);
            const half* fieldZ = this->fieldZ.data;
            const int size1 = imageSize.x *1;
            const int size2 = imageSize.y *size1;
            const int size3 = imageCount.x*size2;
            assert_(imageSize.x == imageSize.y);
            const float zBias = 2./imageSize.x;
            const float m32 = M1(3,2), m33 = M1(3,3);
            const float m00 = M1(0,0), m02 = M1(0,2), m030 = M0(0,3), m03d = M1(0,3)-M0(0,3);
            const float m11 = M1(1,1), m12 = M1(1,2), m130 = M0(1,3), m13d = M1(1,3)-M0(1,3);
            const float m22 = M1(2,2), m23 = M1(2,3);
            const int projectionCount = imageCount.y*imageCount.x;

            for(const uint faceIndex: range(start, start+sizeI)) {
                Scene::Face& face = scene.faces[faceIndex];
                const vec3 a = face.position[0], b = face.position[1], c = face.position[2], d = face.position[3];
                const vec3 O = (a+b+c+d)/4.f;
                const vec3 N = cross(c-a, b-a);
                // Viewpoint st with maximum projection
                vec2 st = clamp(vec2(-1), scale*(O.xy()-scene.viewpoint.xy()) + (scale*(O.z-scene.viewpoint.z)/(N.z==0?0/*-0 negates infinities*/:-N.z))*N.xy(), vec2(1));
                if(!N.z) {
                    if(!N.x) st.x = 0;
                    if(!N.y) st.y = 0;
                }
                // Projects vertices along st view rays on uv plane (perspective)
                mat4 M = shearedPerspective(st[0], st[1], near, far);
                M.scale(scale); // Fits scene within -1, 1
                M.translate(-scene.viewpoint);
                const vec2 uvA = (M*a).xy();
                const vec2 uvB = (M*b).xy();
                const vec2 uvC = (M*c).xy();
                const vec2 uvD = (M*d).xy();
                const float maxU = ::max(length(uvB-uvA), length(uvC-uvD)); // Maximum projected edge length along quad's u axis
                const float maxV = ::max(length(uvD-uvA), length(uvC-uvB)); // Maximum projected edge length along quad's v axis
                const float cellCount = 1;
                const uint U = ceil(maxU*cellCount), V = ceil(maxV*cellCount);
                assert_(U && V);
                // Scales uv for texture sampling (unnormalized)
                for(float& u: face.u) { u *= U; assert_(isNumber(u)); }
                for(float& v: face.v) { v *= V; assert_(isNumber(v)); }
                // Allocates image (FIXME)
                face.image = Image8(U, V);

                const vec3 ab = b-a;
                const vec3 ad = d-a;
                const vec3 badc = a-b+c-d;

                assert_(projectionCount);
                // Integrates surface visibility over projection (Tests surface UV samples against depth buffers)
                for(uint svIndex: range(V)) for(uint suIndex: range(U)) {
                    const float v = (float(svIndex)+1.f/2)/float(V);
                    const float u = (float(suIndex)+1.f/2)/float(U);
                    const vec3 P = a + ad*v + (ab + badc*v) * u;
                    uint hit = 0;
                    for(uint tIndex : range(tSize)) for(uint sIndex: range(sSize)) {
                        const float s = sSizeScale*float(sIndex), t = tSizeScale*float(tIndex);
                        const float iPw = 1.f/(m32*P.z + m33);
                        const float Pu = (m00*P.x + m030 + (m02*P.z + m03d)*s)*iPw;
                        const float Pv = (m11*P.y + m130 + (m12*P.z + m13d)*t)*iPw;
                        const float Pz = (m22*P.z + m23)*iPw;
                        //const float z = fieldZ(sIndex, tIndex, Pu+1.f/2, Pv+1.f/2);
                        const half* Zst = fieldZ + (uint64)tIndex*size3 + sIndex*size2;
                        const int uIndex = Pu+1.f/2;
                        const int vIndex = Pv+1.f/2;
                        if(!(uIndex >= 0 && uint(uIndex) < imageSize.x && vIndex >= 0 && uint(vIndex) < imageSize.y)) continue;
                        assert_(uIndex >= 0 && uint(uIndex) < imageSize.x && vIndex >= 0 && uint(vIndex) < imageSize.y, uIndex, vIndex);
                        const float z = Zst[vIndex*size1 + uIndex]; // -1, 1
                        if(Pz <= z+zBias) hit++;
                    }
                    face.image[svIndex*U+suIndex] = hit*0xFF/projectionCount;
                }
            }
        });
#else
        float cellCount = 0;
        for(string name: folder.list(Files)) {
            TextData s (name);
            const uint cellCount_ = s.integer(false);
            if(!s.match('x')) continue;
            const int sSize_ = s.integer(false);
            if(!s.match('x')) continue;
            const int tSize_ = s.integer(false);
            if(s) continue;
            cellCount = cellCount_;
            sSize = sSize_;
            tSize = tSize_;
            break;
        }
        TexRenderer.shader.stSize = tSize*sSize;
        assert_(cellCount && sSize && tSize);
        surfaceMap = Map(str(uint(cellCount))+'x'+str(sSize)+'x'+str(tSize), folder);
        assert_(surfaceMap.size%(3*tSize*sSize) == 0);
        //const size_t sampleCount = surfaceMap.size / (3*tSize*sSize); // per component
        const ref<uint8> BGR = cast<uint8>(surfaceMap);

        size_t index = 0;
        for(Scene::Face& face: scene.faces) {
            const vec3 a = face.position[0], b = face.position[1], c = face.position[2], d = face.position[3];
            const vec3 O = (a+b+c+d)/4.f;
            const vec3 N = cross(c-a, b-a);
            // Viewpoint st with maximum projection
            vec2 st = clamp(vec2(-1), scale*(O.xy()-scene.viewpoint.xy()) + (scale*(O.z-scene.viewpoint.z)/(N.z==0?0/*-0 negates infinities*/:-N.z))*N.xy(), vec2(1));
            if(!N.z) {
                if(!N.x) st.x = 0;
                if(!N.y) st.y = 0;
            }
            // Projects vertices along st view rays on uv plane (perspective)
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

            // No copy (surface samples needs to stay memory mapped)
            face.BGR = unsafeRef(BGR.slice(index,3*tSize*sSize*V*U));
            face.stride = U;
            face.height = V;

            index += face.BGR.ref::size;
        }
        assert_(index == surfaceMap.size);
#endif
        log(time);

        window = ::window(&view);
        window->actions[Key('f')] = [this]{ displayField=!displayField; window->render(); };
        window->actions[Key('d')] = [this]{ depthCorrect=!depthCorrect; window->render(); };
        window->actions[Key('s')] = [this]{ displaySurfaceParametrized=!displaySurfaceParametrized; window->render(); };
        window->actions[Key('p')] = [this]{ displayParametrization=!displayParametrization; window->render(); };
    }
    Image render(uint2 targetSize) {
        Image target (targetSize);

        // Fits scene
        vec3 min = inff, max = -inff;
        for(const Scene::Face& f: scene.faces) for(vec3 p: f.position) { min = ::min(min, p); max = ::max(max, p); }
        max.z += 0x1p-8; // Prevents back and far plane from Z-fighting
        const float scale = 2./::max(max.x-min.x, max.y-min.y);
        const float near = scale*(-scene.viewpoint.z+min.z);
        const float far = scale*(-scene.viewpoint.z+max.z);

        // Sheared perspective (rectification)
        const float s = view.viewYawPitch.x/(PI/3), t = view.viewYawPitch.y/(PI/3);
        mat4 M = shearedPerspective(s, t, near, far);
        M.scale(scale); // Fits scene within -1, 1
        M.translate(-scene.viewpoint);
        const vec2 st = vec2((s+1)/2, (t+1)/2) * vec2(imageCount-uint2(2));
        const vec2 scaleTargetUV = vec2(imageSize-uint2(1)) / vec2(target.size-uint2(1));

        if(displayField && imageCount) {
            assert_(imageSize.x == imageSize.y && imageCount.x == imageCount.y);

            ImageH Z (target.size);
            if(depthCorrect) scene.render(Zrenderer, M, {}, Z);

            parallel_chunk(target.size.y*target.size.x, [this, &target, near, far, scaleTargetUV, st, &Z](uint, size_t start, size_t sizeI) {
                const int targetStride = target.size.x;
                const uint sIndex = st[0], tIndex = st[1];
                const uint2 imageCount = this->imageCount;
                const uint2 imageSize = this->imageSize;
                const float scale = (float)(imageSize.x-1)/(imageCount.x-1); // st -> uv
                const float A = - scale*(0+(far-near)/(2*far));
                const float B = - scale*(1-(far+near)/(2*far));
                //const half* fieldZ = this->fieldZ.data;
                //const v4sf zTolerance = float4(1); FIXME
                const half* fieldB = this->fieldB.data;
                const half* fieldG = this->fieldG.data;
                const half* fieldR = this->fieldR.data;
                const int size1 = this->size1;
                const int size2 = this->size2;
                const int size3 = this->size3;
                assert_(imageSize.x%2==0); // Gather 32bit / half
                const v2si sample2D = {    0,           size1/2};
                const v8si sample4D = {    0,           size1/2,         size2/2,       (size2+size1)/2,
                                     size3/2,   (size3+size1)/2, (size3+size2)/2, (size3+size2+size1)/2};
                for(size_t targetIndex: range(start, start+sizeI)) {
                    int targetX = targetIndex%targetStride, targetY = targetIndex/targetStride;
                    const vec2 uv = scaleTargetUV * vec2(targetX, targetY);
                    bgr3f S = 0;
                    if(depthCorrect) {
                        const float z = Z(targetX, targetY);// -1, 1
                        assert_(z >= -1 && z <= 1, z);
                        const float d = A*z + B;
                        const v4sf x = {st[1], st[0]}; // ts
                        const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                        const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                        v4sf w01st = __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0) // ttTT
                                   * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1); // sSsS

                        v4sf B = _0f, G = _0f, R = _0f;
                        for(int dt: {0,1}) for(int ds: {0,1}) {
                            if(sIndex+ds > imageCount.x-1) { w01st[dt*2+ds] = 0; continue; } // s == sSize-1
                            if(tIndex+dt > imageCount.y-1) { w01st[dt*2+ds] = 0; continue; } // t == tSize-1
                            vec2 uv_ = uv + d * (fract(st) - vec2(ds, dt));
                            if(uv_[0] < 0 || uv_[1] < 0) { w01st[dt*2+ds] = 0; continue; }
                            uint uIndex = uv_[0], vIndex = uv_[1];
                            if( uIndex >= uint(imageSize.x)-2 || vIndex >= uint(imageSize.y)-2 ) { w01st[dt*2+ds] = 0; continue; }
                            assert_(tIndex < imageCount.x && sIndex < imageCount.y, sIndex, tIndex);
                            const size_t base = (size_t)(tIndex+dt)*size3 + (sIndex+ds)*size2 + vIndex*size1 + uIndex;
                            const v2sf x = {uv_[1], uv_[0]}; // vu
                            const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                            const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
#if 1
                            const v4sf w01uv =   __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)  // vvVV
                                    * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1); // uUuU
#else
                            const v4sf Z = toFloat((v4hf)gather((float*)(fieldZ+base), sample2D));
                            const v4sf w01uv = and(__builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)  // vvVV
                                                   * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1) // uUuU
                                                   , abs(Z - float4(z)) < zTolerance); // Discards far samples (tradeoff between edge and anisotropic accuracy)
#endif
                            float sum = ::sum(w01uv);
                            const v4sf w01 = float4(1./sum) * w01uv; // Renormalizes uv interpolation (in case of discarded samples)
                            w01st[dt*2+ds] *= sum; // Adjusts weight for st interpolation
                            if(!sum) { /*B[dt*2+ds] = 0; G[dt*2+ds] = 0; R[dt*2+ds] = 0; w01st[dt*2+ds] = 0;*/ continue; }
                            B[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldB+base), sample2D)));
                            G[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldG+base), sample2D)));
                            R[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldR+base), sample2D)));
                        }
                        const v4sf w01 = float4(1./sum(w01st)) * w01st; // Renormalizes st interpolation (in case of discarded samples)
                        const float b = dot(w01, B);
                        const float g = dot(w01, G);
                        const float r = dot(w01, R);
                        S = bgr3f(b, g, r);
                    } else {
                        const int uIndex = uv[0], vIndex = uv[1];
                        if(uv[0] < 0 || uv[1] < 0) { target[targetIndex]=byte4(0,0,0xFF,0xFF); continue; }
                        if(uIndex >= int(imageSize.x)-1 || vIndex >= int(imageSize.y)-1) { target[targetIndex]=byte4(0xFF,0xFF,0,0xFF); continue; }
                        //if(tIndex >= int(imageCount.y)-1 || sIndex >= int(imageSize.x)-1) { target[targetIndex]=byte4(0xFF,0xFF,0,0xFF); continue; }
                        //assert_(tIndex < imageCount.x-1 && sIndex < imageCount.y-1, sIndex, tIndex);
                        const size_t base = (size_t)tIndex*size3 + sIndex*size2 + vIndex*size1 + uIndex;
                        const v16sf B = toFloat((v16hf)gather((float*)(fieldB+base), sample4D));
                        const v16sf G = toFloat((v16hf)gather((float*)(fieldG+base), sample4D));
                        const v16sf R = toFloat((v16hf)gather((float*)(fieldR+base), sample4D));

                        const v4sf x = {st[1], st[0], uv[1], uv[0]}; // tsvu
                        const v8sf X = __builtin_shufflevector(x, x, 0,1,2,3, 0,1,2,3);
                        static const v8sf _00001111f = {0,0,0,0,1,1,1,1};
                        const v8sf w_1mw = abs(X - floor(X) - _00001111f); // fract(x), 1-fract(x)
                        const v16sf w01 = shuffle(w_1mw, w_1mw, 4,4,4,4,4,4,4,4, 0,0,0,0,0,0,0,0)  // ttttttttTTTTTTTT
                                        * shuffle(w_1mw, w_1mw, 5,5,5,5,1,1,1,1, 5,5,5,5,1,1,1,1)  // ssssSSSSssssSSSS
                                        * shuffle(w_1mw, w_1mw, 6,6,2,2,6,6,2,2, 6,6,2,2,6,6,2,2)  // vvVVvvVVvvVVvvVV
                                        * shuffle(w_1mw, w_1mw, 7,3,7,3,7,3,7,3, 7,3,7,3,7,3,7,3); // uUuUuUuUuUuUuUuU
                        S = bgr3f(dot(w01, B), dot(w01, G), dot(w01, R));
                    }
                    extern uint8 sRGB_forward[0x1000];
                    target[targetIndex] = byte4(sRGB_forward[uint(S.b*0xFFF)], sRGB_forward[uint(S.g*0xFFF)], sRGB_forward[uint(S.r*0xFFF)], 0xFF);
                }
            });
        } else {
            ImageH B (target.size), G (target.size), R (target.size);
            if(displayParametrization)
                scene.render(UVRenderer, M, (float[]){1,1,1}, {}, B, G, R);
            else if(displaySurfaceParametrized) {
                const uint sIndex = ((s+1)/2)*(sSize-1);
                const uint tIndex = ((t+1)/2)*(tSize-1);
                assert_(sIndex < sSize && tIndex < tSize);
                TexRenderer.shader.stIndex = tIndex*sSize+sIndex;
                scene.render(TexRenderer, M, (float[]){1,1,1}, {}, B, G, R);
            } else {
                BGRRenderer.shader.viewpoint = scene.viewpoint + vec3(s,t,0)/scale;
                scene.render(BGRRenderer, M, (float[]){1,1,1}, {}, B, G, R);
            }
            convert(target, B, G, R);
        }
        return target;
    }
} view;
