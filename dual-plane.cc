#include "thread.h"
#include "string.h"
#include "matrix.h"
#include "interface.h"
#include "window.h"
#include "view-widget.h"
#include "variant.h"
#include "simd.h"
#include "parallel.h"
#include "mwc.h"
#include "camera.h"
#include "renderer/TraceableScene.h"
#include "integrators/TraceBase.h"
#include "sampling/UniformPathSampler.h"
#include "prerender.h"

struct ViewApp : ViewControl {
    string name;
    vec2 min, max;
    uint2 imageCount;
    uint2 imageSize;
    Map map;
    ref<half> field;

    bool orthographic = false;
    bool depthCorrect = true;
    bool depthThreshold = true;
    //bool uniformWeights = false;
    bool wideReconstruction = false;

    unique<Window> window = nullptr;

    TraceableScene scene;

    ImageF sumB, sumG, sumR;
    uint count = 0; // Iteration count (Resets on view angle change)
    vec2 angles = 0;

    struct {
        ImageF sumB, sumG, sumR;
        uint count = 0; // Iteration count (Resets on uv change)
        int2 target = 0;
        //vec2 st = 0, uv_uncorrected = 0;
        //vec2 xyz = 0;
    } st;

    ViewApp() {
        assert_(arguments());
        load(arguments()[0]);
        window = ::window(this);
        window->setTitle(name);
        window->actions[Key('o')] = [this]{ orthographic=!orthographic; count=0; st.count=0; window->render(); };
        window->actions[Key('d')] = [this]{ depthCorrect=!depthCorrect; window->render(); };
        window->actions[Key('t')] = [this]{ depthThreshold=!depthThreshold; window->render(); };
        //window->actions[Key('u')] = [this]{ uniformWeights=!uniformWeights; window->render(); window->setTitle(str(uniformWeights)); };
        window->actions[Key('w')] = [this]{ wideReconstruction=!wideReconstruction; window->render(); window->setTitle(str(wideReconstruction)); };
        st.target = 960/2;
    }
    void load(string name) {
        field = {};
        map = Map();
        imageCount = 0;
        imageSize = 0;
        this->name = name;
        const Folder Tmp {"/var/tmp/light",currentWorkingDirectory(), true};
        Folder tmp (name, Tmp, true);

        for(string name: tmp.list(Files)) {
            //if(name=="32x32x960x960") continue; // WIP
            TextData s (name);
            imageCount.x = s.integer(false);
            if(!s.match('x')) continue;
            imageCount.y = s.integer(false);
            if(!s.match('x')) continue;
            imageSize.x = s.integer(false);
            if(!s.match('x')) continue;
            imageSize.y = s.integer(false);
            assert_(!s);
            map = Map(name, tmp);
            field = cast<half>(map);
            break;
        }
        //assert_(imageCount && imageSize);
        if(!imageCount && !imageSize) imageSize = uint2(960);

        if(window) {
            window->setSize();
            window->setTitle(name);
        }
    }
    virtual vec2 sizeHint(vec2) override { return vec2(2*imageSize.x, imageSize.y+128); }
    virtual shared<Graphics> graphics(vec2) override { render(ViewControl::angles); return shared<Graphics>(); }
    void render(vec2 angles) {
        const Image& window = ((XWindow*)this->window.pointer)->target;
        const Image target = cropShare(window, int2(0), imageSize);
        ImageH Z(imageSize);
        const mat4 camera = parseCamera(readFile("scene.json"));
        mat4 newCamera = camera;
        mat4 projection; // Recover w by projecting from world to homogenous coordinates, as w cannot be recovered from normalized coordinates (after perspective divide: w/w=1)
        float s = 0, t = 0;
        if(orthographic) {
            newCamera.rotateY(angles.x); // Yaw
            newCamera.rotateX(angles.y); // Pitch
            newCamera[3].xyz() = (mat3)newCamera * vec3(0, 0, -2); // Rotation center
        } else {
            s = angles.x/(PI/3), t = angles.y/(PI/3);
            vec4 O = newCamera * vec4(s, t, 0, 0);
            newCamera.translate(vec3(s, t, 0));
            newCamera(0, 2) -= O.x;
            newCamera(1, 2) -= O.y;
            newCamera(2, 2) -= O.z;
            projection(3, 3) = 0;
            projection(3, 2) = 1;
        }
#if 1
        {
            if(this->angles != angles || sumB.size != target.size) {
                this->angles = angles;
                count = 0; // Resets accumulation
            }
            if(count == 0) {
                if(sumB.size != target.size) sumB = ImageF(target.size); sumB.clear(0);
                if(sumG.size != target.size) sumG = ImageF(target.size); sumG.clear(0);
                if(sumR.size != target.size) sumR = ImageF(target.size); sumR.clear(0);
            }
            count++;

            parallel_chunk(target.size.y, [this, camera, newCamera, projection, /*s, t,*/ &Z, &target](uint _threadId, uint start, uint sizeI) {
                TraceBase tracer(scene, _threadId);
                for(int y: range(start, start+sizeI)) for(uint x: range(target.size.x)) {
                    const vec4 Op = vec4((2.f*x/float(imageSize.x-1)-1), ((2.f*y/float(imageSize.y-1)-1)), 0, (projection*vec4(0,0,0,1)).w);
                    const vec4 Pp = vec4((2.f*x/float(imageSize.x-1)-1), ((2.f*y/float(imageSize.y-1)-1)), 1, (projection*vec4(0,0,1,1)).w); // FIXME: sheared perspective
                    const vec3 O = newCamera * (Op.w * Op.xyz());
                    const vec3 P = newCamera * (Pp.w * Pp.xyz());
                    float hitDistance;
                    Vec3f emission = tracer.trace(O, P, hitDistance, /*count*/8);
                    size_t i = y*target.size.x+x;
                    sumR[i] += emission[0];
                    sumG[i] += emission[1];
                    sumB[i] += emission[2];

                    const uint r = 0xFFF*::min(1.f, sumR[i]/count);
                    const uint g = 0xFFF*::min(1.f, sumG[i]/count);
                    const uint b = 0xFFF*::min(1.f, sumB[i]/count);
                    extern uint8 sRGB_forward[0x1000];
                    target[(target.size.y-1-y)*target.stride+x] = byte4(sRGB_forward[b], sRGB_forward[g], sRGB_forward[r], 0xFF);
                    Z[i] = hitDistance / ::length(P-O); // (orthogonal) distance to ST plane
                }
            });
            if(count < 1024) this->window->render();
        }
#endif
#if 1
        if(imageSize && imageCount) {
            const Image target = cropShare(window, int2(imageSize.x,0), imageSize);
            assert_(imageCount.x == imageCount.y);
            parallel_chunk(target.size.y, [this, &target, camera, newCamera, projection, /*s, t,*/ &Z](uint, uint start, uint sizeI) {
                const uint size1 = imageSize.x *1;
                const uint size2 = imageSize.y *size1;
                const uint size3 = imageCount.x*size2;
                const uint64 size4 = uint64(imageCount.y)*uint64(size3);
                const struct Image4DH : ref<half> {
                    uint4 size;
                    Image4DH(uint2 imageCount, uint2 imageSize, ref<half> data) : ref<half>(data), size(imageCount.y, imageCount.x, imageSize.y, imageSize.x) {}
                    const half& operator ()(uint s, uint t, uint u, uint v) const {
                        assert_(t < size[0] && s < size[1] && v < size[2] && u < size[3], int(s), int(t), int(u), int(v));
                        size_t index = ((uint64(t)*size[1]+s)*size[2]+v)*size[3]+u;
                        assert_(index < ref<half>::size, int(index), ref<half>::size, int(s), int(t), int(u), int(v), size);
                        return operator[](index);
                    }
                } fieldZ {imageCount, imageSize, field.slice(0*size4, size4)},
                  fieldB {imageCount, imageSize, field.slice(1*size4, size4)},
                  fieldG {imageCount, imageSize, field.slice(2*size4, size4)},
                  fieldR {imageCount, imageSize, field.slice(3*size4, size4)};
                assert_(imageSize.x%2==0); // Gather 32bit / half
                const v2ui sample2D = {    0,           size1/2};
                const v8ui sample4D = {    0,           size1/2,         size2/2,       (size2+size1)/2,
                                           size3/2, (size3+size1)/2, (size3+size2)/2, (size3+size2+size1)/2};
                const float scale = (float)(imageSize.x-1) / (imageCount.x-1); // st -> uv
                for(int targetY: range(start, start+sizeI)) for(int targetX: range(target.size.x)) {
                    size_t targetIndex = (target.size.y-1-targetY)*target.stride + targetX;
                    const vec4 Op = vec4((2.f*targetX/float(imageSize.x-1)-1), ((2.f*targetY/float(imageSize.y-1)-1)), 0, (projection*vec4(0,0,0,1)).w);
                    const vec4 Pp = vec4((2.f*targetX/float(imageSize.x-1)-1), ((2.f*targetY/float(imageSize.y-1)-1)), 1, (projection*vec4(0,0,1,1)).w); // FIXME: sheared perspective
                    const vec3 Ow = newCamera * (Op.w * Op.xyz());
                    const vec3 Pw = newCamera * (Pp.w * Pp.xyz());
                    const vec3 O = camera.inverse() * Ow;
                    const vec3 P = camera.inverse() * Pw;
                    const vec3 d = normalize(P-O);

                    const vec3 n (0,0,-1);
                    const float nd = dot(n, d);
                    const vec3 n_nd = n / nd;

                    const vec2 Pst = O.xy() + dot(n_nd, vec3(0,0,0)-O) * d.xy();
                    const vec2 ST = (Pst+vec2(1))/2.f;
                    const vec2 Puv = P.xy() + dot(n_nd, vec3(0,0,1)-P) * d.xy();
                    const vec2 UV = (Puv+vec2(1))/2.f;

                    const vec2 st = vec2(0x1p-16) + vec2(1-0x1p-16) * ST * vec2(imageCount-uint2(1));
                    const vec2 uv_uncorrected = vec2(1-0x1p-16) * UV * vec2(imageSize-uint2(1));

                    if(st[0] < -0 || st[1] < -0) { target[targetIndex]=byte4(0xFF,0,0,0xFF); continue; }
                    const uint sIndex = uint(st[0]), tIndex = uint(st[1]);
                    if(sIndex >= uint(imageCount.x)-1 || tIndex >= uint(imageCount.y)-1) { target[targetIndex]=byte4(0,0xFF,0xFF,0xFF); continue; }

                    bgr3f S = 0;
#if 1
                    if(depthCorrect) {
                        const float z = Z(targetX, targetY);
                        const float z_ = z==inff ? 1 : (z-1)/z;
                        const float zW = z;

                        if(!wideReconstruction) { // Bilinear
                            const v4sf x = {st[1], st[0]}; // ts
                            const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                            static const v4sf _0011f = {0,0,1,1};
                            const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                            v4sf w01st = __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0) // ttTT
                                    * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1); // sSsS

                            v4sf B, G, R;
                            for(int dt: {0,1}) for(int ds: {0,1}) {
                                vec2 uv_ = uv_uncorrected + scale * (vec2(ds, dt) - fract(st)) * z_;
                                if(uv_[0] < 0 || uv_[1] < 0) { w01st[dt*2+ds] = 0; continue; }
                                int uIndex = uv_[0], vIndex = uv_[1];
                                if(uIndex >= int(imageSize.x)-1 || vIndex >= int(imageSize.y)-1) { w01st[dt*2+ds] = 0; continue; }
                                const size_t base = (size_t)(tIndex+dt)*size3 + (sIndex+ds)*size2 + vIndex*size1 + uIndex;
                                //assert_(base < fieldZ.ref::size, base, tIndex, dt, sIndex, ds, vIndex, uIndex, uv_, z, (z-2)/(z-1));
                                const v2sf x = {uv_[1], uv_[0]}; // vu
                                const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                                static const v4sf _0011f = {0,0,1,1};
                                const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                                v4sf w01uv;
                                if(!depthThreshold) {
                                    w01uv = __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)    // vvVV
                                          * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1); // uUuU
                                } else {
                                    const v4sf Z = toFloat((v4hf)gather((float*)(fieldZ.data+base), sample2D)); // FIXME
                                    w01uv = and( Z==float4(z)/*inf=inf*/ | abs(Z - float4(zW)) < float4(0x1p-4), // Discards far samples (tradeoff between edge and anisotropic accuracy)
                                                        __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)    // vvVV
                                                        * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1) ); // uUuU
                                }
                                const float sum = ::hsum(w01uv);
                                if(!sum) { w01st[dt*2+ds] = 0; B[dt*2+ds] = 0; G[dt*2+ds] = 0; R[dt*2+ds] = 0; continue; }
                                const v4sf w01 = float4(1./sum) * w01uv; // Renormalizes uv interpolation (in case of discarded samples)
                                w01st[dt*2+ds] *= sum; // Adjusts weight for st interpolation
                                B[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldB.data+base), sample2D)));
                                G[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldG.data+base), sample2D)));
                                R[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldR.data+base), sample2D)));
                                //if(uniformWeights) w01st[dt*2+ds] = w01st[dt*2+ds] ? 1 : 0;
                            }
                            const v4sf w01 = float4(1./hsum(w01st)) * w01st; // Renormalizes st interpolation (in case of discarded samples)
                            const float b = dot(w01, B);
                            const float g = dot(w01, G);
                            const float r = dot(w01, R);
                            S = bgr3f(b, g, r);
                        } else { // Wide reconstruction
                            float b = 0, g = 0, r = 0, w = 0;
                            for(int dt: range(-1,1 +1+1)) for(int ds: range(-1,1 +1+1)) {
                                if(sIndex+ds > uint(imageCount.x)-1 || tIndex+dt > uint(imageCount.y)-1) continue;
                                vec2 uv_ = uv_uncorrected + scale * (vec2(ds, dt) - fract(st)) * z_;
                                if(uv_[0] < 0 || uv_[1] < 0) continue;
                                int uIndex = uv_[0], vIndex = uv_[1];
                                if(uIndex >= int(imageSize.x)-1 || vIndex >= int(imageSize.y)-1) continue;
                                const size_t base = (size_t)(tIndex+dt)*size3 + (sIndex+ds)*size2 + vIndex*size1 + uIndex;
                                const v2sf x = {uv_[1], uv_[0]}; // vu
                                const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                                static const v4sf _0011f = {0,0,1,1};
                                const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                                const v4sf Z = toFloat((v4hf)gather((float*)(fieldZ.data+base), sample2D)); // FIXME
                                const v4sf w01uv = and( (Z==float4(z)/*inf=inf*/) | (abs(Z - float4(zW)) < float4(0x1p-1/**length(Pw-Ow)*/)), // Discards far samples (tradeoff between edge and anisotropic accuracy)
                                                        __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)    // vvVV
                                                        * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1) ); // uUuU
                                const float sum = ::hsum(w01uv);
                                if(!sum) continue;
                                const v4sf w01 = float4(1./sum) * w01uv; // Renormalizes uv interpolation (in case of discarded samples)
                                b += dot(w01, toFloat((v4hf)gather((float*)(fieldB.data+base), sample2D)));
                                g += dot(w01, toFloat((v4hf)gather((float*)(fieldG.data+base), sample2D)));
                                r += dot(w01, toFloat((v4hf)gather((float*)(fieldR.data+base), sample2D)));
                                w += 1; // FIXME: World space reconstruction filter ?
                            }
                            S = bgr3f(b/w, g/w, r/w);
                        }
                    } else
#endif
                    {
                        const uint uIndex = uint(uv_uncorrected[0]), vIndex = uint(uv_uncorrected[1]);
                        if(uv_uncorrected[0] < 0 || uv_uncorrected[1] < 0) { target[targetIndex]=byte4(0,0,0xFF,0xFF); continue; }
                        if(uIndex >= uint(imageSize.x)-1 || vIndex >= uint(imageSize.y)-1) { target[targetIndex]=byte4(0xFF,0xFF,0,0xFF); continue; }
                        const size_t base = uint64(tIndex)*uint64(size3) + sIndex*size2 + vIndex*size1 + uIndex;
                        const v16sf B = toFloat(v16hf(gather(reinterpret_cast<const float*>(fieldB.data+base), sample4D)));
                        const v16sf G = toFloat(v16hf(gather(reinterpret_cast<const float*>(fieldG.data+base), sample4D)));
                        const v16sf R = toFloat(v16hf(gather(reinterpret_cast<const float*>(fieldR.data+base), sample4D)));

                        const v4sf x = {st[1], st[0], uv_uncorrected[1], uv_uncorrected[0]}; // tsvu
                        const v8sf X = __builtin_shufflevector(x, x, 0,1,2,3, 0,1,2,3);
                        static const v8sf _00001111f = {0,0,0,0,1,1,1,1};
                        const v8sf w_1mw = abs(X - floor(X) - _00001111f); // fract(x), 1-fract(x)
                        const v16sf w01 = shuffle(w_1mw, w_1mw, 4,4,4,4,4,4,4,4, 0,0,0,0,0,0,0,0)  // ttttttttTTTTTTTT
                                * shuffle(w_1mw, w_1mw, 5,5,5,5,1,1,1,1, 5,5,5,5,1,1,1,1)  // ssssSSSSssssSSSS
                                * shuffle(w_1mw, w_1mw, 6,6,2,2,6,6,2,2, 6,6,2,2,6,6,2,2)  // vvVVvvVVvvVVvvVV
                                * shuffle(w_1mw, w_1mw, 7,3,7,3,7,3,7,3, 7,3,7,3,7,3,7,3); // uUuUuUuUuUuUuUuU
                        S = bgr3f(dot(w01, B), dot(w01, G), dot(w01, R));
                    }
                    const uint b = ::min(0xFFFu, uint(0xFFF*S.b));
                    const uint g = ::min(0xFFFu, uint(0xFFF*S.g));
                    const uint r = ::min(0xFFFu, uint(0xFFF*S.r));
                    extern uint8 sRGB_forward[0x1000];
                    target[targetIndex] = byte4(sRGB_forward[b], sRGB_forward[g], sRGB_forward[r], 0xFF);
                }
            });
        }
#endif
        {
            const Image target = cropShare(window, int2(0,imageSize.y), imageCount);
            if(st.count == 0) {
                if(st.sumB.size != imageCount) st.sumB = ImageF(imageCount); st.sumB.clear(0);
                if(st.sumG.size != imageCount) st.sumG = ImageF(imageCount); st.sumG.clear(0);
                if(st.sumR.size != imageCount) st.sumR = ImageF(imageCount); st.sumR.clear(0);
            }
            st.count++;
            TraceBase tracer(scene, 0); // TODO: parallel
            {
                const uint size1 = imageSize.x *1;
                const uint size2 = imageSize.y *size1;
                const uint size3 = imageCount.x*size2;
                const uint64 size4 = uint64(imageCount.y)*uint64(size3);
                const struct Image4DH : ref<half> {
                    uint4 size;
                    Image4DH(uint2 imageCount, uint2 imageSize, ref<half> data) : ref<half>(data), size(imageCount.y, imageCount.x, imageSize.y, imageSize.x) {}
                    const half& operator ()(uint s, uint t, uint u, uint v) const {
                        assert_(t < size[0] && s < size[1] && v < size[2] && u < size[3], int(s), int(t), int(u), int(v));
                        size_t index = ((uint64(t)*size[1]+s)*size[2]+v)*size[3]+u;
                        assert_(index < ref<half>::size, int(index), ref<half>::size, int(s), int(t), int(u), int(v), size);
                        return operator[](index);
                    }
                } fieldZ {imageCount, imageSize, field.slice(0*size4, size4)},
                  fieldB {imageCount, imageSize, field.slice(1*size4, size4)},
                  fieldG {imageCount, imageSize, field.slice(2*size4, size4)},
                  fieldR {imageCount, imageSize, field.slice(3*size4, size4)};
                assert_(imageSize.x%2==0); // Gather 32bit / half
                const v2ui sample2D = {    0,           size1/2};
                const float scale = (float)(imageSize.x-1) / (imageCount.x-1); // st -> uv
                const vec4 Op = vec4((2.f*st.target.x/float(imageSize.x-1)-1), ((2.f*st.target.y/float(imageSize.y-1)-1)), 0, (projection*vec4(0,0,0,1)).w);
                const vec4 Pp = vec4((2.f*st.target.x/float(imageSize.x-1)-1), ((2.f*st.target.y/float(imageSize.y-1)-1)), 1, (projection*vec4(0,0,1,1)).w); // FIXME: sheared perspective
                const vec3 Ow = newCamera * (Op.w * Op.xyz());
                const vec3 Pw = newCamera * (Pp.w * Pp.xyz());
                {
                    const vec3 O = camera.inverse() * Ow;
                    const vec3 P = camera.inverse() * Pw;
                    const vec3 d = normalize(P-O);

                    const vec3 n (0,0,-1);
                    const float nd = dot(n, d);
                    const vec3 n_nd = n / nd;

                    const vec2 Pst = O.xy() + dot(n_nd, vec3(0,0,0)-O) * d.xy();
                    const vec2 ST = (Pst+vec2(1))/2.f;
                    const vec2 Puv = P.xy() + dot(n_nd, vec3(0,0,1)-P) * d.xy();
                    const vec2 UV = (Puv+vec2(1))/2.f;

                    const vec2 st = vec2(0x1p-16) + vec2(1-0x1p-16) * ST * vec2(imageCount-uint2(1));
                    const vec2 uv_uncorrected = vec2(1-0x1p-16) * UV * vec2(imageSize-uint2(1));

                    const float z = Z(this->st.target.x, this->st.target.y);
                    const float z_ = z==inff ? 1 : (z-1)/z;
                    const float zW = z; // * length(Pw-Ow);

                    for(int tIndex: range(imageCount.y)) for(int sIndex: range(imageCount.x)) {
                        vec2 uv_ = uv_uncorrected + scale * (vec2(sIndex,tIndex)-st) * z_;
#if 0
                        const vec3 Ow = newCamera * vec3(0, 0, 0);
                        const vec3 Pw = newCamera * vec3((2.f*st.target.x/float(imageSize.x-1)-1), ((2.f*st.target.y/float(imageSize.y-1)-1)), 1);
#else
                        float b = 0, g = 0, r = 0;
                        if(!(uv_[0] < 0 || uv_[1] < 0)) {
                            int uIndex = uv_[0], vIndex = uv_[1];
                            if(!(uIndex >= int(imageSize.x)-1 || vIndex >= int(imageSize.y)-1)) {
                                const size_t base = (size_t)(tIndex)*size3 + (sIndex)*size2 + vIndex*size1 + uIndex;
                                const v2sf x = {uv_[1], uv_[0]}; // vu
                                const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                                static const v4sf _0011f = {0,0,1,1};
                                const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                                v4sf w01uv;
                                if(depthThreshold) {
                                    const v4sf Z = toFloat((v4hf)gather((float*)(fieldZ.data+base), sample2D)); // FIXME
                                    w01uv = and( (Z==float4(z)/*inf=inf*/) | (abs(Z - float4(zW)) < float4(0x1p-1/**length(Pw-Ow)*/)), // Discards far samples (tradeoff between edge and anisotropic accuracy)
                                                        __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)    // vvVV
                                                        * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1) ); // uUuU
                                } else {
                                    w01uv = __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)  // vvVV
                                                 * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1); // uUuU
                                }
                                const float sum = ::hsum(w01uv);
                                if(sum) {
                                    const v4sf w01 = float4(1./sum) * w01uv; // Renormalizes uv interpolation (in case of discarded samples)
                                    b = dot(w01, toFloat((v4hf)gather((float*)(fieldB.data+base), sample2D)));
                                    g = dot(w01, toFloat((v4hf)gather((float*)(fieldG.data+base), sample2D)));
                                    r = dot(w01, toFloat((v4hf)gather((float*)(fieldR.data+base), sample2D)));
                                }
                            }
#endif
                        }
                        const uint R = 0xFFF*::min(1.f, r);
                        const uint G = 0xFFF*::min(1.f, g);
                        const uint B = 0xFFF*::min(1.f, b);
                        extern uint8 sRGB_forward[0x1000];
                        target[(tIndex*2+0)*target.stride+sIndex*2+0] = target[(tIndex*2+0)*target.stride+sIndex*2+1] =
                        target[(tIndex*2+1)*target.stride+sIndex*2+0] = target[(tIndex*2+1)*target.stride+sIndex*2+1] = byte4(sRGB_forward[B], sRGB_forward[G], sRGB_forward[R], 0xFF);
                    }
                }
            }
            /*for(int tIndex: range(imageCount.y)) for(int sIndex: range(imageCount.x)) {
                const float s = 2*(sIndex/float(imageCount.x-1))-1, t = 2*(tIndex/float(imageCount.y-1))-1;
                const vec3 O = newCamera * vec3(s, t, 0);
                const vec3 P = newCamera * vec3(st.uv, 1); // TODO: uv correction
                float hitDistance;
                Vec3f emission = tracer.trace(O, P, hitDistance, 16);
                float Z = hitDistance / ::length(P-O);
                size_t i = tIndex*imageCount.x+sIndex;
                st.sumR[i] += emission[0];
                st.sumG[i] += emission[1];
                st.sumB[i] += emission[2];

                const uint r = 0xFFF*::min(1.f, st.sumR[i]/st.count);
                const uint g = 0xFFF*::min(1.f, st.sumG[i]/st.count);
                const uint b = 0xFFF*::min(1.f, st.sumB[i]/st.count);
                extern uint8 sRGB_forward[0x1000];
                target[(tIndex*2+0)*target.stride+sIndex*2+0] = target[(tIndex*2+0)*target.stride+sIndex*2+1] =
                target[(tIndex*2+1)*target.stride+sIndex*2+0] = target[(tIndex*2+1)*target.stride+sIndex*2+1] = byte4(sRGB_forward[b], sRGB_forward[g], sRGB_forward[r], 0xFF);
            }*/
        }
    }
    virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& widget) override {
        if(event == Press) {
            st.target = int2(cursor.x, 960-1-cursor.y);
            /*const mat4 camera = parseCamera(readFile("scene.json"));
            mat4 newCamera = camera;
            float s = 0, t = 0;
            if(orthographic) {
                newCamera.rotateY(angles.x); // Yaw
                newCamera.rotateX(angles.y); // Pitch
                newCamera[3].xyz() = (mat3)newCamera * vec3(0, 0, -2);
            } else {
                s = angles.x/(PI/3), t = angles.y/(PI/3);
            }

            {
                const vec3 Ow = newCamera * vec3(s, t, 0);
                const vec3 Pw = newCamera * vec3((2.f*targetX/float(target.size.x-1)-1), ((2.f*targetY/float(target.size.y-1)-1)), 1);
                const vec3 O = camera.inverse() * Ow;
                const vec3 P = camera.inverse() * Pw;
                const vec3 d = normalize(P-O);

                const vec3 n (0,0,-1);
                const float nd = dot(n, d);
                const vec3 n_nd = n / nd;

                const vec2 Pst = O.xy() + dot(n_nd, vec3(0,0,0)-O) * d.xy();
                const vec2 ST = (Pst+vec2(1))/2.f;
                const vec2 Puv = P.xy() + dot(n_nd, vec3(0,0,1)-P) * d.xy();
                const vec2 UV = (Puv+vec2(1))/2.f;

                st.st = vec2(0x1p-16) + vec2(1-0x1p-16) * ST * vec2(imageCount-uint2(1));
                st.uv_uncorrected = vec2(1-0x1p-16) * UV * vec2(imageSize-uint2(1));
            }*/
            /*const vec3 O = newCamera * vec3(s, t, 0);
            const vec3 P = newCamera * vec3((2.f*x/float(target.size.x-1)-1), ((2.f*y/float(target.size.y-1)-1)), 1); // FIXME: sheared perspective
            float hitDistance;
            tracer.trace(O, P, hitDistance, 0);
            st.xyz = O + hitDistance*normalize(P-O);
            //st.uv = vec2((2.f*cursor.x/float(imageSize.x-1)-1), -((2.f*cursor.y/float(imageSize.y-1)-1)));*/
            st.count = 0;
        }
        return ViewControl::mouseEvent(cursor, size, event, button, widget);
    }
} view;
