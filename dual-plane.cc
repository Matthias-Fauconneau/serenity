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

    //ViewWidget view {uint2(512), {this, &ViewApp::render}};
    unique<Window> window = nullptr;

    TraceableScene scene;

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
    virtual vec2 sizeHint(vec2) override { return vec2(2*512, 512); }
    virtual shared<Graphics> graphics(vec2) override { render(angles); return shared<Graphics>(); }
    void render(vec2 angles) {
        const Image& window = ((XWindow*)this->window.pointer)->target;
        const Image target = cropShare(window, int2(0), uint2(512));
        ImageH Z(uint2(512));
#if 1
        {
            const mat4 camera = parseCamera(readFile("scene.json"));
            const float s = (angles.x+::PI/3)/(2*::PI/3), t = (angles.y+::PI/3)/(2*::PI/3);
            const mat4 M = shearedPerspective(s, t) * camera;
            parallel_chunk(target.size.y, [this, &target, M, &Z](uint _threadId, uint start, uint sizeI) {
                UniformPathSampler sampler(readCycleCounter());
                TraceBase tracer(&scene, TraceSettings(), 0);
                for(int y: range(start, start+sizeI)) for(uint x: range(target.size.x)) {
                    sampler.startPath(y*target.stride+x, 0);
                    const vec3 O = M.inverse() * vec3(2.f*x/float(target.size.x-1)-1, -(2.f*y/float(target.size.y-1)-1), -1);
                    PositionSample position;
                    position.p.x() = O.x;
                    position.p.y() = O.y;
                    position.p.z() = O.z;
                    position.weight = Vec3f(1.0f);
                    position.pdf = 1.0f;
                    position.Ng = scene._camera->_transform.fwd();
                    DirectionSample direction;
                    const vec3 P = M.inverse() * vec3(2.f*x/float(target.size.x-1)-1, -(2.f*y/float(target.size.y-1)-1), +1);
                    const vec3 d = normalize(P-O);
                    direction.d.x() = d.x;
                    direction.d.y() = d.y;
                    direction.d.z() = d.z;
                    direction.weight = Vec3f(1.0f);
                    direction.pdf = 1;

                    Vec3f throughput (1);
                    Ray ray(position.p, direction.d);
                    ray.setPrimaryRay(true);

                    MediumSample mediumSample;
                    SurfaceScatterEvent event;
                    IntersectionTemporary data;
                    Medium::MediumState state;
                    state.reset();
                    IntersectionInfo info;
                    Vec3f emission(0.0f);
                    const Medium* medium = 0;

                    float hitDistance = inff;
                    bool wasSpecular = true;
                    for(int bounce = 0;;bounce++) {
                        info.primitive = nullptr;
                        data.primitive = nullptr;
                        TraceableScene::IntersectionRay eRay(convert(ray), data, ray, scene._userGeomId);
                        rtcIntersect(scene._scene, eRay);

                        bool didHit;
                        if (data.primitive) {
                            info.p = ray.pos() + ray.dir()*ray.farT();
                            info.w = ray.dir();
                            info.epsilon = scene.DefaultEpsilon;
                            data.primitive->intersectionInfo(data, info);
                            didHit = true;
                        } else {
                            didHit = false;
                        }
                        constexpr int maxBounces = 16;
                        if((!didHit && !medium) || bounce >= maxBounces) {
                            if(scene.intersectInfinites(ray, data, info)) {
                                if(wasSpecular || !info.primitive->isSamplable())
                                    emission += throughput*info.primitive->evalDirect(data, info);
                            }
                            break;
                        }

                        bool hitSurface = true;
                        if (medium) {
                            if (!medium->sampleDistance(sampler, ray, state, mediumSample))
                                break;
                            throughput *= mediumSample.weight;
                            hitSurface = mediumSample.exited;
                            if(hitSurface && !didHit) {
                                if(scene.intersectInfinites(ray, data, info)) {
                                    if(wasSpecular || !info.primitive->isSamplable())
                                        emission += throughput*info.primitive->evalDirect(data, info);
                                }
                                break;
                            }
                        }

                        if (hitSurface) {
                            if(bounce == 0) hitDistance = ray.farT();

                            {
                                TangentFrame frame;
                                info.primitive->setupTangentFrame(data, info, frame);

                                event = SurfaceScatterEvent(
                                            &info,
                                            &sampler,
                                            frame,
                                            frame.toLocal(-ray.dir()),
                                            BsdfLobes::AllLobes,
                                            false
                                            );
                            }
                            Vec3f transmittance(-1.0f);

                            const Bsdf &bsdf = *info.bsdf;
                            Vec3f transparency = bsdf.eval(event.makeForwardEvent(), false);
                            float transparencyScalar = transparency.avg();

                            Vec3f wo;
                            if (event.sampler->nextBoolean(transparencyScalar) ){
                                wo = ray.dir();
                                event.pdf = transparencyScalar;
                                event.weight = transparency/transparencyScalar;
                                event.sampledLobe = BsdfLobes::ForwardLobe;
                                throughput *= event.weight;
                            } else {
                                if (bounce < maxBounces - 1 && !(event.info->bsdf->lobes().isPureSpecular() || event.info->bsdf->lobes().isForward())) {
                                    assert(scene.lights().size() == 1);
                                    const Primitive& light = *scene.lights()[0].get();
                                    Vec3f result (0.0f);
                                    LightSample sample;
                                    if(light.sampleDirect(_threadId, event.info->p, *event.sampler, /*out*/ sample)) {
                                        event.wo = event.frame.toLocal(sample.d);

                                        bool geometricBackside = (sample.d.dot(event.info->Ng) < 0.0f);
                                        medium = event.info->primitive->selectMedium(medium, geometricBackside);

                                        event.requestedLobe = BsdfLobes::AllButSpecular;

                                        Vec3f f = event.info->bsdf->eval(event, false);
                                        if(f != 0.0f) {

                                            Ray lightRay = ray.scatter(event.info->p, sample.d, event.info->epsilon);
                                            lightRay.setPrimaryRay(false);

                                            IntersectionTemporary data;
                                            IntersectionInfo info;
                                            const float expectedDist = sample.dist;
                                            constexpr float fudgeFactor = 1.0f + 1e-3f;

                                            if(light.intersect(lightRay, data) && lightRay.farT()*fudgeFactor >= expectedDist) {
                                                info.p = lightRay.pos() + lightRay.dir()*lightRay.farT();
                                                info.w = lightRay.dir();
                                                light.intersectionInfo(data, info);
                                                Vec3f shadow = tracer.generalizedShadowRay(sampler, lightRay, medium, &light, bounce);
                                                transmittance = shadow;
                                                if(shadow != 0.0f) {
                                                    Vec3f e = shadow*light.evalDirect(data, info);
                                                    if (e != 0.0f) {
                                                        result += f*e/sample.pdf*powerHeuristic(sample.pdf, event.info->bsdf->pdf(event));
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    event.requestedLobe = BsdfLobes::AllButSpecular;
                                    if(event.info->bsdf->sample(event, false) && event.weight != 0.0f) {
                                        Vec3f wo = event.frame.toGlobal(event.wo);

                                        bool geometricBackside = (wo.dot(event.info->Ng) < 0.0f);
                                        medium = event.info->primitive->selectMedium(medium, geometricBackside);

                                        Ray bsdfRay = ray.scatter(event.info->p, wo, event.info->epsilon);
                                        bsdfRay.setPrimaryRay(false);

                                        IntersectionTemporary data;
                                        IntersectionInfo info;
                                        const float expectedDist = sample.dist;
                                        constexpr float fudgeFactor = 1.0f + 1e-3f;

                                        if(light.intersect(bsdfRay, data) && bsdfRay.farT()*fudgeFactor >= expectedDist) {
                                            info.p = bsdfRay.pos() + bsdfRay.dir()*bsdfRay.farT();
                                            info.w = bsdfRay.dir();
                                            light.intersectionInfo(data, info);
                                            Vec3f shadow = tracer.generalizedShadowRay(sampler, bsdfRay, medium, &light, bounce);
                                            transmittance = shadow;
                                            if(shadow != 0.0f) {
                                                Vec3f e = shadow*light.evalDirect(data, info);
                                                if (e != 0.0f) {
                                                    result += e*event.weight*powerHeuristic(event.pdf, light.directPdf(_threadId, data, info, event.info->p));
                                                }
                                            }
                                        }
                                    }
                                    emission += result*throughput;
                                }
                                if (info.primitive->isEmissive()) {
                                    if (wasSpecular || !info.primitive->isSamplable())
                                        emission += info.primitive->evalDirect(data, info)*throughput;
                                }

                                event.requestedLobe = BsdfLobes::AllLobes;
                                if (!bsdf.sample(event, false)) break;

                                wo = event.frame.toGlobal(event.wo);

                                throughput *= event.weight;
                                wasSpecular = event.sampledLobe.hasSpecular();
                                if (!wasSpecular)
                                    ray.setPrimaryRay(false);
                            }

                            bool geometricBackside = (wo.dot(info.Ng) < 0.0f);
                            medium = info.primitive->selectMedium(medium, geometricBackside);
                            state.reset();

                            ray = ray.scatter(ray.hitpoint(), wo, info.epsilon);
                        }

                        if (throughput.max() == 0.0f) {
                            if(scene.intersectInfinites(ray, data, info)) {
                                if(wasSpecular || !info.primitive->isSamplable())
                                    emission += throughput*info.primitive->evalDirect(data, info);
                            }
                            break;
                        }

                        float roulettePdf = std::abs(throughput).max();
                        if (bounce > 2 && roulettePdf < 0.1f) {
                            if (sampler.nextBoolean(roulettePdf))
                                throughput /= roulettePdf;
                            else
                                break;
                        }
                    }
                    const uint r = 0xFFF*::min(1.f, emission[0]);
                    const uint g = 0xFFF*::min(1.f, emission[1]);
                    const uint b = 0xFFF*::min(1.f, emission[2]);
                    extern uint8 sRGB_forward[0x1000];
                    target[y*target.stride+x] = byte4(sRGB_forward[b], sRGB_forward[g], sRGB_forward[r], 0xFF);
                    Z[y*target.size.x+x] = hitDistance;
                }
            });
        }
#endif
#if 1
        {
            const Image target = cropShare(window, int2(512,0), uint2(512,512));
            mat4 M;
            if(orthographic) {
                M.rotateX(angles.y); // Pitch
                M.rotateY(angles.x); // Yaw
                M.scale(vec3(1,1,-1)); // Z-
            } else {
                const float s = (angles.x+PI/3)/(2*PI/3), t = (angles.y+PI/3)/(2*PI/3);
                M = shearedPerspective(s, t);
            }
            assert_(imageCount.x == imageCount.y);
            parallel_chunk(target.size.y, [this, &target, M, &Z](uint, uint start, uint sizeI) {
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
                    size_t targetIndex = targetY*target.stride + targetX;
                    const vec3 O = M.inverse() * vec3(2.f*targetX/float(target.size.x-1)-1, 2.f*targetY/float(target.size.y-1)-1, -1);
                    const vec3 P = M.inverse() * vec3(2.f*targetX/float(target.size.x-1)-1, 2.f*targetY/float(target.size.y-1)-1, +1);
                    const vec3 d = normalize(P-O);

                    const vec3 n (0,0,1);
                    const float nd = dot(n, d);
                    const vec3 n_nd = n / nd;

                    const vec2 Pst = O.xy() + dot(n_nd, vec3(0,0,1)-O) * d.xy();
                    const vec2 ST = (Pst+vec2(1))/2.f;
                    const vec2 Puv = O.xy() + dot(n_nd, vec3(0,0,0)-O) * d.xy();
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
                        if(z <= 1) continue; // FIXME
                        //const float z_ = z-1.f/2;
                        const float z_ = z==inff ? 1 : (z-2)/(z-1);

                        const v4sf x = {st[1], st[0]}; // ts
                        const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                        static const v4sf _0011f = {0,0,1,1};
                        const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                        v4sf w01st = __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0) // ttTT
                                * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1); // sSsS

                        v4sf B, G, R;
                        for(int dt: {0,1}) for(int ds: {0,1}) {
                            vec2 uv_ = uv_uncorrected + scale * (fract(st) - vec2(ds, dt)) * z_; //(z-2)/(z-1); //(-z_) / (z_+2);
                            if(uv_[0] < 0 || uv_[1] < 0) { w01st[dt*2+ds] = 0; continue; }
                            int uIndex = uv_[0], vIndex = uv_[1];
                            if(uIndex >= int(imageSize.x)-1 || vIndex >= int(imageSize.y)-1) { w01st[dt*2+ds] = 0; continue; }
                            const size_t base = (size_t)(tIndex+dt)*size3 + (sIndex+ds)*size2 + vIndex*size1 + uIndex;
                            assert_(base < fieldZ.ref::size, base, tIndex, dt, sIndex, ds, vIndex, uIndex, uv_, z, (z-2)/(z-1));
                            const v2sf x = {uv_[1], uv_[0]}; // vu
                            const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                            static const v4sf _0011f = {0,0,1,1};
                            const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                            const v4sf Z = toFloat((v4hf)gather((float*)(fieldZ.data+base), sample2D)); // FIXME
                            const v4sf w01uv = and( abs(Z - float4(z)) < float4(0x1p-5), // Discards far samples (tradeoff between edge and anisotropic accuracy)
                                                    __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)    // vvVV
                                                  * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1) ); // uUuU
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
                    //target[targetIndex] = byte4(byte3(float(0xFF)*S), 0xFF);
                }
            });
        }
#endif
    }
} view;
