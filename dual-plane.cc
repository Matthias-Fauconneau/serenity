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
//#include "prerender.h"

struct ViewApp : ViewControl {
    string name;
    vec2 min, max;
    uint2 imageCount;
    uint2 imageSize;
    Map map;
    ref<half> field;

    bool orthographic = false;

    unique<Window> window = nullptr;

    TraceableScene scene;

    ImageF sumB, sumG, sumR;
    uint count = 0; // Iteration count (Resets on view angle change)
    vec2 angles = 0;


    ViewApp() {
        assert_(arguments());
        load(arguments()[0]);
        window = ::window(this);
        window->setTitle(name);
        window->actions[Key('o')] = [this]{ orthographic=!orthographic; window->render(); };
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
        assert_(imageCount && imageSize);

        if(window) {
            window->setSize();
            window->setTitle(name);
        }
    }
    virtual vec2 sizeHint(vec2) override { return vec2(2*imageSize.x, imageSize.y); }
    virtual shared<Graphics> graphics(vec2) override { render(angles); return shared<Graphics>(); }
    void render(vec2 angles) {
        const Image& window = ((XWindow*)this->window.pointer)->target;
        const Image target = cropShare(window, int2(0), imageSize);
        ImageH Z(imageSize);
        const mat4 camera = parseCamera(readFile("scene.json"));
        const float s = angles.x/(PI/3), t = angles.y/(PI/3);
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

            parallel_chunk(target.size.y, [this, &target, camera,/*M,*/ &Z, s, t](uint _threadId, uint start, uint sizeI) {
                TraceBase tracer(scene, _threadId);
                for(int y: range(start, start+sizeI)) for(uint x: range(target.size.x)) {
                    const vec3 O = camera * vec3(s, t, 0);
                    const vec3 P = camera * vec3((2.f*x/float(target.size.x-1)-1), ((2.f*y/float(target.size.y-1)-1)), 1);
                    float hitDistance;
                    Vec3f emission = tracer.trace(O, P, hitDistance);
                    size_t i = y*target.size.x+x;
                    sumR[i] += emission[0];
                    sumG[i] += emission[1];
                    sumB[i] += emission[2];

                    const uint r = 0xFFF*::min(1.f, sumR[i]/count);
                    const uint g = 0xFFF*::min(1.f, sumG[i]/count);
                    const uint b = 0xFFF*::min(1.f, sumB[i]/count);
                    extern uint8 sRGB_forward[0x1000];
                    target[(target.size.y-1-y)*target.stride+x] = byte4(sRGB_forward[b], sRGB_forward[g], sRGB_forward[r], 0xFF);
                    Z[i] = hitDistance / ::length(P-O);
                }
            });
            if(count < 1024) this->window->render();
        }
#endif
#if 1
        {
            const Image target = cropShare(window, int2(imageSize.x,0), imageSize);
            /*mat4 M;
            if(orthographic) {
                M.rotateX(angles.y); // Pitch
                M.rotateY(angles.x); // Yaw
                M.scale(vec3(1,1,-1)); // Z-
            } else {
                const float s = (angles.x+PI/3)/(2*PI/3), t = (angles.y+PI/3)/(2*PI/3);
                M = shearedPerspective(s, t);
            }*/
            assert_(imageCount.x == imageCount.y);
            parallel_chunk(target.size.y, [this, &target, camera, s, t, &Z](uint, uint start, uint sizeI) {
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
                const unused v2ui sample2D = {    0,           size1/2};
                const v8ui sample4D = {    0,           size1/2,         size2/2,       (size2+size1)/2,
                                           size3/2, (size3+size1)/2, (size3+size2)/2, (size3+size2+size1)/2};
                const float scale = (float) imageSize.x / imageCount.x; // st -> uv
                for(int targetY: range(start, start+sizeI)) for(int targetX: range(target.size.x)) {
                    size_t targetIndex = (target.size.y-1-targetY)*target.stride + targetX;
                    const vec3 O = camera.inverse() * camera * vec3(s, t, 0);
                    const vec3 P = camera.inverse() * camera * vec3((2.f*targetX/float(target.size.x-1)-1), ((2.f*targetY/float(target.size.y-1)-1)), 1);
                    const vec3 d = normalize(P-O);

                    const vec3 n (0,0,-1);
                    const float nd = dot(n, d);
                    const vec3 n_nd = n / nd;

                    const vec2 Pst = O.xy() + dot(n_nd, vec3(0,0,0)-O) * d.xy();
                    const vec2 ST = (Pst+vec2(1))/2.f;
                    const vec2 Puv = O.xy() + dot(n_nd, vec3(0,0,1)-O) * d.xy();
                    const vec2 UV = (Puv+vec2(1))/2.f;

                    const vec2 st = vec2(0x1p-16) + vec2(1-0x1p-16) * ST * vec2(imageCount-uint2(1));
                    const vec2 uv_uncorrected = vec2(1-0x1p-16) * UV * vec2(imageSize-uint2(1));

                    if(st[0] < -0 || st[1] < -0) { target[targetIndex]=byte4(0xFF,0,0,0xFF); continue; }
                    const uint sIndex = uint(st[0]), tIndex = uint(st[1]);
                    if(sIndex >= uint(imageCount.x)-1 || tIndex >= uint(imageCount.y)-1) { target[targetIndex]=byte4(0,0xFF,0xFF,0xFF); continue; }

                    bgr3f S = 0;
#if 1
                    if(1) {
                        const float z = Z(targetX, targetY);
                        const float z_ = z==inff ? 1 : (z-1)/z;

                        const v4sf x = {st[1], st[0]}; // ts
                        const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                        static const v4sf _0011f = {0,0,1,1};
                        const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                        v4sf w01st = __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0) // ttTT
                                * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1); // sSsS

                        v4sf B, G, R;
                        for(int dt: {0,1}) for(int ds: {0,1}) {
                            vec2 uv_ = uv_uncorrected + scale * (fract(st) - vec2(ds, dt)) * -z_;
                            if(uv_[0] < 0 || uv_[1] < 0) { w01st[dt*2+ds] = 0; continue; }
                            int uIndex = uv_[0], vIndex = uv_[1];
                            if(uIndex >= int(imageSize.x)-1 || vIndex >= int(imageSize.y)-1) { w01st[dt*2+ds] = 0; continue; }
                            const size_t base = (size_t)(tIndex+dt)*size3 + (sIndex+ds)*size2 + vIndex*size1 + uIndex;
                            assert_(base < fieldZ.ref::size, base, tIndex, dt, sIndex, ds, vIndex, uIndex, uv_, z, (z-2)/(z-1));
                            const v2sf x = {uv_[1], uv_[0]}; // vu
                            const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                            static const v4sf _0011f = {0,0,1,1};
                            const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
#if 0
                            const v4sf w01uv = __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)    // vvVV
                                             * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1) ); // uUuU
#else
                            const v4sf Z = toFloat((v4hf)gather((float*)(fieldZ.data+base), sample2D)); // FIXME
                            const v4sf w01uv = and( Z==float4(z)/*inf=inf*/ || abs(Z - float4(z)) < float4(1/*0x1p-5*/), // Discards far samples (tradeoff between edge and anisotropic accuracy)
                                                    __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)    // vvVV
                                                  * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1) ); // uUuU
#endif
                            float sum = ::hsum(w01uv);
                            const v4sf w01 = float4(1./sum) * w01uv; // Renormalizes uv interpolation (in case of discarded samples)
                            w01st[dt*2+ds] *= sum; // Adjusts weight for st interpolation
                            if(!sum) { B[dt*2+ds] = 0; G[dt*2+ds] = 0; R[dt*2+ds] = 0; continue; }
                            B[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldB.data+base), sample2D)));
                            G[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldG.data+base), sample2D)));
                            R[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldR.data+base), sample2D)));
                        }
                        const v4sf w01 = float4(1./hsum(w01st)) * w01st; // Renormalizes st interpolation (in case of discarded samples)
                        const float b = dot(w01, B);
                        const float g = dot(w01, G);
                        const float r = dot(w01, R);
                        S = bgr3f(b, g, r);
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
    }
} view;
