#include "io/Scene.h"
#include "integrators/TraceBase.h"
#include "sampling/UniformPathSampler.h"
#include "matrix.h"
#include "interface.h"
#include "window.h"
#include "view-widget.h"
#include "variant.h"
#include "simd.h"
#include "parallel.h"
#include "mwc.h"

inline Variant parseJSON(TextData& s) {
    s.whileAny(" \t\n\r"_);
    if(s.match("true")) return true;
    else if(s.match("false")) return false;
    else if(s.match('"')) {
        return copyRef(s.until('"'));
    }
    else if(s.match('{')) {
        Dict dict;
        s.whileAny(" \t\n\r"_);
        if(!s.match('}')) for(;;) {
            s.skip('"');
            string key = s.until('"');
            assert_(key && !dict.contains(key));
            s.whileAny(" \t\n\r"_);
            s.skip(':');
            Variant value = parseJSON(s);
            dict.insertSorted(copyRef(key), ::move(value));
            s.whileAny(" \t\n\r"_);
            if(s.match(',')) { s.whileAny(" \t\n\r"_); continue; }
            if(s.match('}')) break;
            error("Expected , or }"_);
        }
        return dict;
    }
    else if(s.match('[')) {
        array<Variant> list;
        s.whileAny(" \t\n\r"_);
        if(!s.match(']')) for(;;) {
            Variant value = parseJSON(s);
            list.append( ::move(value) );
            s.whileAny(" \t\n\r"_);
            if(s.match(',')) continue;
            if(s.match(']')) break;
            error("Expected , or ]"_);
        }
        return list;
    }
    else {
        string d = s.whileDecimal();
        if(d) return parseDecimal(d);
        else error("Unexpected"_, s.peek(16));
    }
}

const vec2 Vec2(ref<Variant> v) { return vec2((float)v[0],(float)v[1]); }
const vec3 Vec3(ref<Variant> v) { return vec3((float)v[0],(float)v[1],(float)v[2]); }

const mat4 transform(const Dict& object) {
    const Dict& t = object.at("transform");
    mat4 transform;
    const vec3 look_at = Vec3(t.at("look_at"));
    const vec3 position = Vec3(t.at("position"));
    const vec3 z = normalize(look_at - position);
    vec3 y = Vec3(t.at("up"));
    y = normalize(y - dot(y,z)*z); // Projects up on plane orthogonal to z
    const vec3 x = cross(y, z);
    transform[0] = vec4(x, 0);
    transform[1] = vec4(y, 0);
    transform[2] = vec4(z, 0);
    return transform;
}

mat4 parseCamera(ref<byte> file) {
    TextData s (file);
    Variant root = parseJSON(s);
    const Dict& camera = root.dict.at("camera");
    mat4 modelView = ::transform( camera ).inverse();
    modelView = /*mat4().translate(vec3(0,0,-1./2)) **/ mat4().scale(1./16) * modelView;
    modelView.rotateZ(PI); // -Z (FIXME)
    modelView = mat4().rotateZ(PI) * modelView;
    return modelView;
}

static mat4 shearedPerspective(const float s, const float t) { // Sheared perspective (rectification)
    const float S = 2*s-1, T = 2*t-1; // [0,1] -> [-1, 1]
    const float left = (-1-S), right = (1-S);
    const float bottom = (-1-T), top = (1-T);
    mat4 M;
    M(0,0) = 2 / (right-left);
    M(1,1) = 2 / (top-bottom);
    M(0,2) = (right+left) / (right-left);
    M(1,2) = (top+bottom) / (top-bottom);
    const float near = 1-1./2, far = 1+1./2;
    M(2,2) = - (far+near) / (far-near);
    M(2,3) = - 2*far*near / (far-near);
    M(3,2) = - 1;
    M(3,3) = 0;
    M.translate(vec3(-S,-T,0));
    M.translate(vec3(0,0,-1)); // 0 -> -1 (Z-)
    return M;
}

static Folder tmp {"/var/tmp/light",currentWorkingDirectory(), true};

#if 0
struct Render {
    Render() {
        Folder cacheFolder {"teapot", tmp, true};
        for(string file: cacheFolder.list(Files)) remove(file, cacheFolder);

        const int N = 17;
        uint2 size (512);

        File file(str(N)+'x'+str(N)+'x'+strx(size), cacheFolder, Flags(ReadWrite|Create));
        size_t byteSize = 4ull*N*N*size.y*size.x*sizeof(half);
        assert_(byteSize <= 16ull*1024*1024*1024);
        file.resize(byteSize);
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<half> field = mcast<half>(map);
        assert_(field.size == 4ull*N*N*size.y*size.x);
        //field.clear(); // Explicitly clears to avoid performance skew from clear on page faults (and forces memory allocation)

        mat4 camera = parseCamera(readFile("scene.json"));

        EmbreeUtil::initDevice();
        ThreadUtils::startThreads(8);
        std::unique_ptr<Scene> scene;
        scene.reset(Scene::load(Path("scene.json")));
        scene.loadResources();
        scene.rendererSettings().setSpp(1);
        scene.camera()->_res.x() = size.x;
        scene.camera()->_res.y() = size.y;
        std::unique_ptr<TraceableScene> flattenedScene;
        flattenedScene.reset(scene.makeTraceable(readCycleCounter()));
        flattenedScene._cam._res.x() = size.x;
        flattenedScene._cam._res.y() = size.y;
        Time time (true); Time lastReport (true);
        //parallel_for(0, N*N, [&](uint unused threadID, size_t stIndex) {
        for(int stIndex: range(N*N)) {
            int sIndex = stIndex%N, tIndex = stIndex/N;
            if(lastReport.seconds()>1) { log(strD(stIndex,N*N)); lastReport.reset(); }

            // Sheared perspective (rectification)
            const float s = sIndex/float(N-1), t = tIndex/float(N-1);
            const mat4 M = shearedPerspective(s, t) * camera;
            flattenedScene._cam.M = M;
            parallel_chunk(size.y, [&flattenedScene, M, size, tIndex, sIndex, field](uint, uint start, uint sizeI) {
                //ImageH Z (unsafeRef(field.slice(((0ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
                ImageH B (unsafeRef(field.slice(((1ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
                ImageH G (unsafeRef(field.slice(((2ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
                ImageH R (unsafeRef(field.slice(((3ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
                half* const targetB = B.begin();
                half* const targetG = G.begin();
                half* const targetR = R.begin();
                UniformSampler sampler(readCycleCounter());
                PathTracer tracer(flattenedScene.get(), PathTracerSettings(), 0);
                for(int y: range(start, start+sizeI)) for(uint x: range(size.x)) {
                    uint pixelIndex = y*size.x + x;
                    sampler.startPath(pixelIndex, 0);
                    const vec3 O = M.inverse() * vec3(2.f*x/float(size.x-1)-1, -(2.f*y/float(size.y-1)-1), -1);
                    PositionSample position;
                    position.p.x() = O.x;
                    position.p.y() = O.y;
                    position.p.z() = O.z;
                    position.weight = Vec3f(1.0f);
                    position.pdf = 1.0f;
                    position.Ng = flattenedScene._cam._transform.fwd();
                    DirectionSample direction;
                    const vec3 P = M.inverse() * vec3(2.f*x/float(size.x-1)-1, -(2.f*y/float(size.y-1)-1), +1);
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
                    SurfaceScatterEvent surfaceEvent;
                    IntersectionTemporary data;
                    Medium::MediumState state;
                    state.reset();
                    IntersectionInfo info;
                    Vec3f emission(0.0f);
                    const Medium *medium = flattenedScene.cam().medium().get();

                    float hitDistance = 0.0f;
                    int bounce = 0;
                    bool didHit = flattenedScene.intersect(ray, data, info);
                    bool wasSpecular = true;
                    while ((didHit || medium) && bounce < maxBounces) {
                        bool hitSurface = true;
                        if (medium) {
                            if (!medium->sampleDistance(sampler, ray, state, mediumSample))
                                goto done;
                            throughput *= mediumSample.weight;
                            hitSurface = mediumSample.exited;
                            if (hitSurface && !didHit)
                                break;
                        }

                        if (hitSurface) {
                            hitDistance += ray.farT();

                            surfaceEvent = tracer.makeLocalScatterEvent(data, info, ray, &sampler);
                            Vec3f transmittance(-1.0f);
                            bool terminate = !tracer.handleSurface(surfaceEvent, data, info, medium, bounce, false, true, ray, throughput, emission, wasSpecular, state, &transmittance);

                            if (terminate)
                                goto done;
                        } else {
                            if (!tracer.handleVolume(sampler, mediumSample, medium, bounce, false, true, ray, throughput, emission, wasSpecular))
                                goto done;
                        }

                        if (throughput.max() == 0.0f)
                            break;

                        float roulettePdf = std::abs(throughput).max();
                        if (bounce > 2 && roulettePdf < 0.1f) {
                            if (sampler.nextBoolean(roulettePdf))
                                throughput /= roulettePdf;
                            else
                                goto done;
                        }

                        bounce++;
                        if (bounce < maxBounces)
                            didHit = flattenedScene.intersect(ray, data, info);
                    }
                    if (bounce < maxBounces)
                        tracer.handleInfiniteLights(data, info, true, ray, throughput, wasSpecular, emission);
done:;
                    targetB[pixelIndex] = emission[2];
                    targetG[pixelIndex] = emission[1];
                    targetR[pixelIndex] = emission[0];
                }
            });
        }
        log("Rendered",strx(uint2(N)),"x",strx(size),"images in", time);
    }
} prerender;
#else

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
            EmbreeUtil::initDevice();
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
                auto result = instantiateCamera(JsonUtils::as<std::string>(camera->value, "type"), camera->value);
                if (result)
                    _camera = std::move(result);
            }

            if (integrator != v.MemberEnd() && integrator->value.IsObject()) {
                auto result = instantiateIntegrator(JsonUtils::as<std::string>(integrator->value, "type"), integrator->value);
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

    Random random;

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
    Image render(uint2 targetSize, vec2 angles) {
        Image target (targetSize);

#if 1
        const mat4 camera = parseCamera(readFile("scene.json"));
        const float s = (angles.x+::PI/3)/(2*::PI/3), t = (angles.y+::PI/3)/(2*::PI/3);
        const mat4 M = shearedPerspective(s, t) * camera;
        extern uint8 sRGB_forward[0x1000];
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
#if 1
                bool wasSpecular = true;
                for(int bounce = 0;;bounce++) {
                    info.primitive = nullptr;
                    data.primitive = nullptr;
                    TraceableScene::IntersectionRay eRay(EmbreeUtil::convert(ray), data, ray, flattenedScene._userGeomId);
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

                        //surfaceEvent = tracer.makeLocalScatterEvent(data, info, ray, &sampler);
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

                        //bool terminate = !tracer.handleSurface(surfaceEvent, data, info, medium, bounce, false, true, ray, throughput, emission, wasSpecular, state, &transmittance);

                        const Bsdf &bsdf = *info.bsdf;

                        // For forward events, the transport direction does not matter (since wi = -wo)
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
                                //float weight; const Primitive *light = tracer.chooseLight(*event.sampler, event.info->p, weight);
                                /*Primitive *light;
                                if (scene.lights().empty())
                                    return nullptr;
                                if (flattenedScene.lights().size() == 1) {
                                    weight = 1.0f;
                                    light = flattenedScene.lights()[0].get();
                                }float total = 0.0f;
                                unsigned numNonNegative = 0;
                                for (size_t i = 0; i < _lightPdf.size(); ++i) {
                                    _lightPdf[i] = scene.lights()[i]->approximateRadiance(_threadId, p);
                                    if (_lightPdf[i] >= 0.0f) {
                                        total += _lightPdf[i];
                                        numNonNegative++;
                                    }
                                }
                                if (numNonNegative == 0) {
                                    for (size_t i = 0; i < _lightPdf.size(); ++i)
                                        _lightPdf[i] = 1.0f;
                                    total = _lightPdf.size();
                                } else if (numNonNegative < _lightPdf.size()) {
                                    for (size_t i = 0; i < _lightPdf.size(); ++i) {
                                        float uniformWeight = (total == 0.0f ? 1.0f : total)/numNonNegative;
                                        if (_lightPdf[i] < 0.0f) {
                                            _lightPdf[i] = uniformWeight;
                                            total += uniformWeight;
                                        }
                                    }
                                }
                                if (total == 0.0f)
                                    return nullptr;
                                float t = sampler.next1D()*total;
                                for (size_t i = 0; i < _lightPdf.size(); ++i) {
                                    if (t < _lightPdf[i] || i == _lightPdf.size() - 1) {
                                        weight = total/_lightPdf[i];
                                        return scene.lights()[i].get();
                                    } else {
                                        t -= _lightPdf[i];
                                    }
                                }*/
                                assert(flattenedScene.lights().size() == 1);
                                const Primitive& light = *flattenedScene.lights()[0].get();
                                /*Vec3f result = tracer.lightSample(*light, event, medium, bounce, ray, &transmittance)
                                             + tracer.bsdfSample(*light, event, medium, bounce, ray);*/
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
                                        //Vec3f e = tracer.attenuatedEmission(*event.sampler, light, medium, sample.dist, data, info, bounce, lightRay, transmittance);
                                        const float expectedDist = sample.dist;
                                        constexpr float fudgeFactor = 1.0f + 1e-3f;

                                        //if (light.isDirac()) lightRay.setFarT(expectedDist);
                                        if(light.intersect(lightRay, data) && lightRay.farT()*fudgeFactor >= expectedDist) {
                                            info.p = lightRay.pos() + lightRay.dir()*lightRay.farT();
                                            info.w = lightRay.dir();
                                            light.intersectionInfo(data, info);

#if 1
                                            Vec3f shadow = tracer.generalizedShadowRay(sampler, lightRay, medium, &light, bounce);
#else
                                            const Medium* shadowRayMedium = medium;
                                            Vec3f shadow (0.0f);
                                            IntersectionTemporary data;
                                            IntersectionInfo info;

                                            float initialFarT = lightRay.farT();
                                            Vec3f throughput(1.0f);
                                            for(;;) {
                                                //bool didHit = flattenedScene.intersect(lightRay, data, info) && info.primitive != endCap;
                                                info.primitive = nullptr;
                                                data.primitive = nullptr;
                                                TraceableScene::IntersectionRay eRay(EmbreeUtil::convert(lightRay), data, lightRay, flattenedScene._userGeomId);
                                                rtcIntersect(flattenedScene.scene, eRay);

                                                bool didHit;
                                                if (data.primitive) {
                                                    info.p = lightRay.pos() + lightRay.dir()*lightRay.farT();
                                                    info.w = lightRay.dir();
                                                    info.epsilon = flattenedScene.DefaultEpsilon;
                                                    data.primitive->intersectionInfo(data, info);
                                                    didHit = info.primitive != &light;
                                                } else {
                                                    didHit = false;
                                                }

                                                if (didHit) {
                                                    if (!info.bsdf->lobes().hasForward()) break;

                                                    //SurfaceScatterEvent event = makeLocalScatterEvent(data, info, lightRay, nullptr);
                                                    TangentFrame frame;
                                                    info.primitive->setupTangentFrame(data, info, frame);

                                                    SurfaceScatterEvent event = SurfaceScatterEvent(
                                                                &info,
                                                                &sampler,
                                                                frame,
                                                                frame.toLocal(-ray.dir()),
                                                                BsdfLobes::AllLobes,
                                                                false
                                                                );

                                                    // For forward events, the transport direction does not matter (since wi = -wo)
                                                    Vec3f transparency = info.bsdf->eval(event.makeForwardEvent(), false);
                                                    if (transparency == 0.0f) break;

                                                    throughput *= transparency;
                                                    bounce++;

                                                    if (bounce >= maxBounces) break;
                                                }

                                                if (shadowRayMedium) throughput *= shadowRayMedium->transmittance(sampler, lightRay);
                                                if (info.primitive == nullptr || info.primitive == &light) {
                                                    shadow = throughput;
                                                    break;
                                                }
                                                shadowRayMedium = info.primitive->selectMedium(shadowRayMedium, !info.primitive->hitBackside(data));

                                                lightRay.setPos(lightRay.hitpoint());
                                                initialFarT -= lightRay.farT();
                                                lightRay.setNearT(info.epsilon);
                                                lightRay.setFarT(initialFarT);
                                            }
#endif
                                            transmittance = shadow;
                                            if(shadow != 0.0f) {
                                                Vec3f e = shadow*light.evalDirect(data, info);
                                                if (e != 0.0f) {
                                                    result += f*e/sample.pdf*SampleWarp::powerHeuristic(sample.pdf, event.info->bsdf->pdf(event));
                                                }
                                            }
                                        }
                                    }
                                }
                                //result += tracer.bsdfSample(light, event, medium, bounce, ray);
                                event.requestedLobe = BsdfLobes::AllButSpecular;
                                if(event.info->bsdf->sample(event, false) && event.weight != 0.0f) {
                                    Vec3f wo = event.frame.toGlobal(event.wo);

                                    bool geometricBackside = (wo.dot(event.info->Ng) < 0.0f);
                                    medium = event.info->primitive->selectMedium(medium, geometricBackside);

                                    Ray bsdfRay = ray.scatter(event.info->p, wo, event.info->epsilon);
                                    bsdfRay.setPrimaryRay(false);

                                    IntersectionTemporary data;
                                    IntersectionInfo info;
                                    //Vec3f e = attenuatedEmission(*event.sampler, light, medium, -1.0f, data, info, bounce, bsdfRay, nullptr);
                                    const float expectedDist = sample.dist;
                                    constexpr float fudgeFactor = 1.0f + 1e-3f;

                                    //if (light.isDirac()) bsdfRay.setFarT(expectedDist);
                                    if(light.intersect(bsdfRay, data) && bsdfRay.farT()*fudgeFactor >= expectedDist) {
                                        info.p = bsdfRay.pos() + bsdfRay.dir()*bsdfRay.farT();
                                        info.w = bsdfRay.dir();
                                        light.intersectionInfo(data, info);
                                        Vec3f shadow = tracer.generalizedShadowRay(sampler, bsdfRay, medium, &light, bounce);
                                        transmittance = shadow;
                                        if(shadow != 0.0f) {
                                            Vec3f e = shadow*light.evalDirect(data, info);
                                            if (e != 0.0f) {
                                                result += e*event.weight*SampleWarp::powerHeuristic(event.pdf, light.directPdf(_threadId, data, info, event.info->p));
                                            }
                                        }
                                    }
                                }
                                emission += result*/*weight**/throughput;
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
#else
                bool wasSpecular = true;
                for(int bounce = 0;; bounce++) {
                    info.primitive = nullptr;
                    data.primitive = nullptr;
                    TraceableScene::IntersectionRay eRay(EmbreeUtil::convert(ray), data, ray, flattenedScene._userGeomId);
                    rtcIntersect(flattenedScene.scene, eRay);

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
                    if((!didHit && !medium) || bounce >= maxBounces) break;

                    bool hitSurface = true;
                    if (medium) {
                        if(!medium->sampleDistance(sampler, ray, state, mediumSample)) break;
                        throughput *= mediumSample.weight;
                        hitSurface = mediumSample.exited;
                        if (hitSurface && !didHit) {
                            //if (bounce >= _settings.minBounces && bounce < _settings.maxBounces)
                            tracer.handleInfiniteLights(data, info, true, ray, throughput, wasSpecular, emission);
                        }
                    }

                    if (hitSurface) {
                        assert_(info.primitive);

                        if(bounce == 0) hitDistance = ray.farT();

                        surfaceEvent = tracer.makeLocalScatterEvent(data, info, ray, &sampler);
                        Vec3f transmittance(-1.0f);
                        if(!tracer.handleSurface(surfaceEvent, data, info, medium, bounce, false, true, ray, throughput, emission, /*out*/ wasSpecular, state, &transmittance))
                            break;

                    } else {
                        if(!tracer.handleVolume(sampler, mediumSample, medium, bounce, false, true, ray, throughput, emission, wasSpecular))
                            break;
                    }

                    if (throughput.max() == 0.0f) {
                        //break;
                        tracer.handleInfiniteLights(data, info, true, ray, throughput, wasSpecular, emission);
                    }

                    float roulettePdf = std::abs(throughput).max();
                    if (bounce > 2 && roulettePdf < 0.1f) {
                        if (sampler.nextBoolean(roulettePdf))
                            throughput /= roulettePdf;
                        else
                            break;
                    }
                }
#endif
                const uint r = ::min(0xFFFu, uint(0xFFF*emission[0]));
                const uint g = ::min(0xFFFu, uint(0xFFF*emission[1]));
                const uint b = ::min(0xFFFu, uint(0xFFF*emission[2]));
                target[pixelIndex] = byte4(sRGB_forward[b], sRGB_forward[g], sRGB_forward[r], 0xFF);
            }
        });
        return unsafeShare(target);
#else
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
        parallel_chunk(target.size.y, [this, &target, M](uint, uint start, uint sizeI) {
            const uint targetStride = target.size.x;
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
            } fieldB {imageCount, imageSize, field.slice(1*size4, size4)},
                     fieldG {imageCount, imageSize, field.slice(2*size4, size4)},
                            fieldR {imageCount, imageSize, field.slice(3*size4, size4)};
            assert_(imageSize.x%2==0); // Gather 32bit / half
            const v8ui sample4D = {    0,           size1/2,         size2/2,       (size2+size1)/2,
                                       size3/2, (size3+size1)/2, (size3+size2)/2, (size3+size2+size1)/2};
            for(int targetY: range(start, start+sizeI)) for(int targetX: range(target.size.x)) {
                size_t targetIndex = targetY*targetStride + targetX;
                const vec3 O = M.inverse() * vec3(2.f*targetX/float(targetStride-1)-1, 2.f*targetY/float(target.size.y-1)-1, -1);
                const vec3 P = M.inverse() * vec3(2.f*targetX/float(targetStride-1)-1, 2.f*targetY/float(target.size.y-1)-1, +1);
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
#endif
        return target;
    }
} view;
#endif
