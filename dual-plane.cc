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

#include "io/Scene.h"
#include "integrators/TraceBase.h"
#include "sampling/UniformPathSampler.h"

static Folder tmp {"/var/tmp/light",currentWorkingDirectory(), true};

struct ViewApp {
    string name;
    vec2 min, max;
    uint2 imageCount;
    uint2 imageSize;
    Map map;
    ref<half> field;

    bool orthographic = true;

    ViewWidget view {uint2(512), {this, &ViewApp::render}};
    unique<Window> window = nullptr;

    struct Scene : ::Scene {
        Scene() : ::Scene(Path(), std::make_shared<TextureCache>()) {
            initDevice();
            std::string json = FileUtils::loadText(_path = Path("scene.json"));
            rapidjson::Document document;
            document.Parse<0>(json.c_str());
             //fromJson(document, *this);
            const rapidjson::Value& v = document;
            JsonSerializable::fromJson(v, (Scene&)*this);

            auto media      = v.FindMember("media");
            auto bsdfs      = v.FindMember("bsdfs");
            auto primitives = v.FindMember("primitives");
            auto camera     = v.FindMember("camera");
            auto integrator = v.FindMember("integrator");
            auto renderer   = v.FindMember("renderer");

            if (media != v.MemberEnd() && media->value.IsArray())
                loadObjectList(media->value, std::bind(&Scene::instantiateMedium, (Scene*)this,
                        std::placeholders::_1, std::placeholders::_2), _media);

            if (bsdfs != v.MemberEnd() && bsdfs->value.IsArray())
                loadObjectList(bsdfs->value, std::bind(&Scene::instantiateBsdf, (Scene*)this,
                        std::placeholders::_1, std::placeholders::_2), _bsdfs);

            if (primitives != v.MemberEnd() && primitives->value.IsArray())
                loadObjectList(primitives->value, std::bind(&Scene::instantiatePrimitive, (Scene*)this,
                        std::placeholders::_1, std::placeholders::_2), _primitives);

            if (camera != v.MemberEnd() && camera->value.IsObject()) {
                auto result = instantiateCamera(as<std::string>(camera->value, "type"), camera->value);
                if (result)
                    _camera = std::move(result);
            }

            if (integrator != v.MemberEnd() && integrator->value.IsObject()) {
                auto result = instantiateIntegrator(as<std::string>(integrator->value, "type"), integrator->value);
                if (result)
                    _integrator = std::move(result);
            }

            if (renderer != v.MemberEnd() && renderer->value.IsObject())
                _rendererSettings.fromJson(renderer->value, *this);

            for (const std::shared_ptr<Medium> &b : _media)
                b->loadResources();
            for (const std::shared_ptr<Bsdf> &b : _bsdfs)
                b->loadResources();
            for (const std::shared_ptr<Primitive> &t : _primitives)
                t->loadResources();

            _camera->loadResources();
            _integrator->loadResources();
            _rendererSettings.loadResources();

            _textureCache->loadResources();

            for (size_t i = 0; i < _primitives.size(); ++i) {
                auto helperPrimitives = _primitives[i]->createHelperPrimitives();
                if (!helperPrimitives.empty()) {
                    _primitives.reserve(_primitives.size() + helperPrimitives.size());
                    for (size_t t = 0; t < helperPrimitives.size(); ++t) {
                        _helperPrimitives.insert(helperPrimitives[t].get());
                        _primitives.emplace_back(std::move(helperPrimitives[t]));
                    }
                }
            }
        }
    } scene;
    TraceableScene flattenedScene {*scene._camera, *scene._integrator, scene._primitives, scene._bsdfs, scene._media, scene._rendererSettings, (uint32)readCycleCounter()};

    ViewApp() {
        assert_(arguments());
        load(arguments()[0]);
        window = ::window(&view);
        window->setTitle(name);
        window->actions[Key('o')] = [this]{ orthographic=!orthographic; window->render(); };
    }
    void load(string name) {
        field = {};
        map = Map();
        imageCount = 0;
        imageSize = 0;
        this->name = name;
        Folder tmp (name, ::tmp, true);

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
    void render(vec2 angles) {
        const Image& target = ((XWindow*)window.pointer)->target;
        const mat4 camera = parseCamera(readFile("scene.json"));
        const float s = (angles.x+::PI/3)/(2*::PI/3), t = (angles.y+::PI/3)/(2*::PI/3);
        const mat4 M = shearedPerspective(s, t) * camera;
        parallel_chunk(target.size.y, [this, &target, M](uint _threadId, uint start, uint sizeI) {
            UniformPathSampler sampler(readCycleCounter());
            TraceBase tracer(&flattenedScene, TraceSettings(), 0);
            for(int y: range(start, start+sizeI)) for(uint x: range(target.size.x)) {
                uint pixelIndex = y*target.size.x + x;
                sampler.startPath(pixelIndex, 0);
                const vec3 O = M.inverse() * vec3(2.f*x/float(target.size.x-1)-1, -(2.f*y/float(target.size.y-1)-1), -1);
                PositionSample position;
                position.p.x() = O.x;
                position.p.y() = O.y;
                position.p.z() = O.z;
                position.weight = Vec3f(1.0f);
                position.pdf = 1.0f;
                position.Ng = flattenedScene._cam._transform.fwd();
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
                const Medium *medium = flattenedScene.cam().medium().get();

                float hitDistance = 0.0f;
                bool wasSpecular = true;
                for(int bounce = 0;;bounce++) {
                    info.primitive = nullptr;
                    data.primitive = nullptr;
                    TraceableScene::IntersectionRay eRay(convert(ray), data, ray, flattenedScene._userGeomId);
                    rtcIntersect(flattenedScene._scene, eRay);

                    bool didHit;
                    if (data.primitive) {
                        info.p = ray.pos() + ray.dir()*ray.farT();
                        info.w = ray.dir();
                        info.epsilon = flattenedScene.DefaultEpsilon;
                        data.primitive->intersectionInfo(data, info);
                        didHit = true;
                    } else {
                        didHit = false;
                    }
                    constexpr int maxBounces = 64;
                    if((!didHit && !medium) || bounce >= maxBounces) {
                        if(flattenedScene.intersectInfinites(ray, data, info)) {
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
                            if(flattenedScene.intersectInfinites(ray, data, info)) {
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
                                assert(flattenedScene.lights().size() == 1);
                                const Primitive& light = *flattenedScene.lights()[0].get();
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
                        if(flattenedScene.intersectInfinites(ray, data, info)) {
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

                const uint r = ::min(0xFFFu, uint(0xFFF*emission[0]));
                const uint g = ::min(0xFFFu, uint(0xFFF*emission[1]));
                const uint b = ::min(0xFFFu, uint(0xFFF*emission[2]));
                extern uint8 sRGB_forward[0x1000];
                target[pixelIndex] = byte4(sRGB_forward[b], sRGB_forward[g], sRGB_forward[r], 0xFF);
            }
        });
    }
} view;
