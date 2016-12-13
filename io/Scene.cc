#include "Scene.h"
#include "DirectoryChange.h"
#include "JsonUtils.h"
#include "FileUtils.h"

#include "phasefunctions/HenyeyGreensteinPhaseFunction.h"
#include "phasefunctions/IsotropicPhaseFunction.h"
#include "phasefunctions/RayleighPhaseFunction.h"

#include "integrators/path_tracer/PathTraceIntegrator.h"

#include "primitives/InfiniteSphereCap.h"
#include "primitives/InfiniteSphere.h"
#include "primitives/TriangleMesh.h"
#include "primitives/Skydome.h"
#include "primitives/Sphere.h"
#include "primitives/Curves.h"
#include "primitives/Point.h"
#include "primitives/Cube.h"
#include "primitives/Quad.h"
#include "primitives/Disk.h"

#include "materials/ConstantTexture.h"
#include "materials/CheckerTexture.h"
#include "materials/DiskTexture.h"
#include "materials/IesTexture.h"

#include "cameras/PinholeCamera.h"

#include "media/HomogeneousMedium.h"
#include "media/AtmosphericMedium.h"
#include "media/ExponentialMedium.h"
#include "media/VoxelMedium.h"

#include "bcsdfs/LambertianFiberBcsdf.h"
#include "bcsdfs/RoughWireBcsdf.h"
#include "bcsdfs/HairBcsdf.h"

#include "bsdfs/RoughDielectricBsdf.h"
#include "bsdfs/RoughConductorBsdf.h"
#include "bsdfs/RoughPlasticBsdf.h"
#include "bsdfs/TransparencyBsdf.h"
#include "bsdfs/DielectricBsdf.h"
#include "bsdfs/SmoothCoatBsdf.h"
#include "bsdfs/RoughCoatBsdf.h"
#include "bsdfs/ConductorBsdf.h"
#include "bsdfs/OrenNayarBsdf.h"
#include "bsdfs/ThinSheetBsdf.h"
#include "bsdfs/ForwardBsdf.h"
#include "bsdfs/LambertBsdf.h"
#include "bsdfs/PlasticBsdf.h"
#include "bsdfs/MirrorBsdf.h"
#include "bsdfs/ErrorBsdf.h"
#include "bsdfs/PhongBsdf.h"
#include "bsdfs/MixedBsdf.h"
#include "bsdfs/NullBsdf.h"
#include "bsdfs/Bsdf.h"

#include "grids/VdbGrid.h"

#include "io/JsonObject.h"

#include "Debug.h"

#include <rapidjson/document.h>
#include <functional>

namespace Tungsten {

Scene::Scene()
: _errorBsdf(std::make_shared<ErrorBsdf>()),
  _errorTexture(std::make_shared<ConstantTexture>(Vec3f(1.0f, 0.0f, 0.0f))),
  _textureCache(std::make_shared<TextureCache>()),
  _camera(std::make_shared<PinholeCamera>()),
  _integrator(std::make_shared<PathTraceIntegrator>())
{
}

Scene::Scene(const Path &srcDir, std::shared_ptr<TextureCache> cache)
: _srcDir(srcDir),
  _errorBsdf(std::make_shared<ErrorBsdf>()),
  _errorTexture(std::make_shared<ConstantTexture>(Vec3f(1.0f, 0.0f, 0.0f))),
  _textureCache(std::move(cache)),
  _camera(std::make_shared<PinholeCamera>()),
  _integrator(std::make_shared<PathTraceIntegrator>())
{
}

Scene::Scene(const Path &srcDir,
      std::vector<std::shared_ptr<Primitive>> primitives,
      std::vector<std::shared_ptr<Bsdf>> bsdfs,
      std::shared_ptr<TextureCache> cache,
      std::shared_ptr<Camera> camera)
: _srcDir(srcDir),
  _primitives(std::move(primitives)),
  _bsdfs(std::move(bsdfs)),
  _errorBsdf(std::make_shared<ErrorBsdf>()),
  _errorTexture(std::make_shared<ConstantTexture>(Vec3f(1.0f, 0.0f, 0.0f))),
  _textureCache(std::move(cache)),
  _camera(std::move(camera)),
  _integrator(std::make_shared<PathTraceIntegrator>())
{
}

std::shared_ptr<PhaseFunction> Scene::instantiatePhase(std::string type, const rapidjson::Value &value) const
{
    std::shared_ptr<PhaseFunction> result;
    if (type == "isotropic")
        result = std::make_shared<IsotropicPhaseFunction>();
    else if (type == "henyey_greenstein")
        result = std::make_shared<HenyeyGreensteinPhaseFunction>();
    else if (type == "rayleigh")
        result = std::make_shared<RayleighPhaseFunction>();
    else {
        DBG("Unkown phase function type: '%s'", type.c_str());
        return nullptr;
    }
    result->fromJson(value, *this);
    return result;
}

std::shared_ptr<Medium> Scene::instantiateMedium(std::string type, const rapidjson::Value &value) const
{
    std::shared_ptr<Medium> result;
    if (type == "homogeneous")
        result = std::make_shared<HomogeneousMedium>();
    else if (type == "atmosphere")
        result = std::make_shared<AtmosphericMedium>();
    else if (type == "exponential")
        result = std::make_shared<ExponentialMedium>();
    else if (type == "voxel")
        result = std::make_shared<VoxelMedium>();
    else {
        DBG("Unkown medium type: '%s'", type.c_str());
        return nullptr;
    }
    result->fromJson(value, *this);
    return result;
}

std::shared_ptr<Grid> Scene::instantiateGrid(std::string type, const rapidjson::Value &value) const
{
    std::shared_ptr<Grid> result;
#if OPENVDB_AVAILABLE
    if (type == "vdb")
        result = std::make_shared<VdbGrid>();
    else
#endif
    {
        DBG("Unkown grid type: '%s'", type.c_str());
        return nullptr;
    }
    result->fromJson(value, *this);
    return result;
}

std::shared_ptr<Bsdf> Scene::instantiateBsdf(std::string type, const rapidjson::Value &value) const
{
    std::shared_ptr<Bsdf> result;
    if (type == "lambert")
        result = std::make_shared<LambertBsdf>();
    else if (type == "phong")
        result = std::make_shared<PhongBsdf>();
    else if (type == "mixed")
        result = std::make_shared<MixedBsdf>();
    else if (type == "dielectric")
        result = std::make_shared<DielectricBsdf>();
    else if (type == "conductor")
        result = std::make_shared<ConductorBsdf>();
    else if (type == "mirror")
        result = std::make_shared<MirrorBsdf>();
    else if (type == "rough_conductor")
        result = std::make_shared<RoughConductorBsdf>();
    else if (type == "rough_dielectric")
        result = std::make_shared<RoughDielectricBsdf>();
    else if (type == "smooth_coat")
        result = std::make_shared<SmoothCoatBsdf>();
    else if (type == "null")
        result = std::make_shared<NullBsdf>();
    else if (type == "forward")
        result = std::make_shared<ForwardBsdf>();
    else if (type == "thinsheet")
        result = std::make_shared<ThinSheetBsdf>();
    else if (type == "oren_nayar")
        result = std::make_shared<OrenNayarBsdf>();
    else if (type == "plastic")
        result = std::make_shared<PlasticBsdf>();
    else if (type == "rough_plastic")
        result = std::make_shared<RoughPlasticBsdf>();
    else if (type == "rough_coat")
        result = std::make_shared<RoughCoatBsdf>();
    else if (type == "transparency")
        result = std::make_shared<TransparencyBsdf>();
    else if (type == "lambertian_fiber")
        result = std::make_shared<LambertianFiberBcsdf>();
    else if (type == "rough_wire")
        result = std::make_shared<RoughWireBcsdf>();
    else if (type == "hair")
        result = std::make_shared<HairBcsdf>();
    else {
        DBG("Unkown bsdf type: '%s'", type.c_str());
        return nullptr;
    }
    result->fromJson(value, *this);
    return result;
}

std::shared_ptr<Primitive> Scene::instantiatePrimitive(std::string type, const rapidjson::Value &value) const
{
    std::shared_ptr<Primitive> result;
    if (type == "mesh")
        result = std::make_shared<TriangleMesh>();
    else if (type == "sphere")
        result = std::make_shared<Sphere>();
    else if (type == "quad")
        result = std::make_shared<Quad>();
    else if (type == "disk")
        result = std::make_shared<Disk>();
    else if (type == "infinite_sphere")
        result = std::make_shared<InfiniteSphere>();
    else if (type == "infinite_sphere_cap")
        result = std::make_shared<InfiniteSphereCap>();
    else if (type == "curves")
        result = std::make_shared<Curves>();
    else if (type == "cube")
        result = std::make_shared<Cube>();
    else if (type == "skydome")
        result = std::make_shared<Skydome>();
    else if (type == "point")
        result = std::make_shared<Point>();
    else {
        DBG("Unknown primitive type: '%s'", type.c_str());
        return nullptr;
    }

    result->fromJson(value, *this);
    return result;
}

std::shared_ptr<Camera> Scene::instantiateCamera(std::string type, const rapidjson::Value &value) const
{
    std::shared_ptr<Camera> result;
    if (type == "pinhole")
        result = std::make_shared<PinholeCamera>();
    else {
        DBG("Unknown camera type: '%s'", type.c_str());
        return nullptr;
    }

    result->fromJson(value, *this);
    return result;
}

std::shared_ptr<Integrator> Scene::instantiateIntegrator(std::string, const rapidjson::Value &value) const
{
    std::shared_ptr<Integrator> result = std::make_shared<PathTraceIntegrator>();
    result->fromJson(value, *this);
    return result;
}

std::shared_ptr<Texture> Scene::instantiateTexture(std::string type, const rapidjson::Value &value, TexelConversion conversion) const
{
    std::shared_ptr<Texture> result;
    if (type == "bitmap")
        return _textureCache->fetchTexture(value, conversion, this);
    else if (type == "constant")
        result = std::make_shared<ConstantTexture>();
    else if (type == "checker")
        result = std::make_shared<CheckerTexture>();
    else if (type == "disk")
        result = std::make_shared<DiskTexture>();
    else if (type == "ies")
        return _textureCache->fetchIesTexture(value, this);
    else {
        DBG("Unkown texture type: '%s'", type.c_str());
        return nullptr;
    }

    result->fromJson(value, *this);
    return result;
}

template<typename Instantiator, typename Element>
void Scene::loadObjectList(const rapidjson::Value &container, Instantiator instantiator, std::vector<std::shared_ptr<Element>> &result)
{
    for (unsigned i = 0; i < container.Size(); ++i) {
        if (container[i].IsObject()) {
            auto element = instantiator(JsonUtils::as<std::string>(container[i], "type"), container[i]);
            if (element)
                result.push_back(std::move(element));
        } else {
            DBG("Don't know what to do with non-object in object list");
        }
    }
}

template<typename T>
std::shared_ptr<T> Scene::findObject(const std::vector<std::shared_ptr<T>> &list, std::string name) const
{
    for (const std::shared_ptr<T> &t : list)
        if (t->name() == name)
            return t;
    FAIL("Unable to find object '%s'", name.c_str());
    return nullptr;
}

template<typename T, typename Instantiator>
std::shared_ptr<T> Scene::fetchObject(const std::vector<std::shared_ptr<T>> &list, const rapidjson::Value &v, Instantiator instantiator) const
{
    if (v.IsString()) {
        return findObject(list, v.GetString());
    } else if (v.IsObject()) {
        return instantiator(JsonUtils::as<std::string>(v, "type"), v);
    } else {
        FAIL("Unkown value type");
        return nullptr;
    }
}

std::shared_ptr<PhaseFunction> Scene::fetchPhase(const rapidjson::Value &v) const
{
    return instantiatePhase(JsonUtils::as<std::string>(v, "type"), v);
}

std::shared_ptr<Medium> Scene::fetchMedium(const rapidjson::Value &v) const
{
    return fetchObject(_media, v, std::bind(&Scene::instantiateMedium, this,
            std::placeholders::_1, std::placeholders::_2));
}

std::shared_ptr<Grid> Scene::fetchGrid(const rapidjson::Value &v) const
{
    return instantiateGrid(JsonUtils::as<std::string>(v, "type"), v);
}

std::shared_ptr<Bsdf> Scene::fetchBsdf(const rapidjson::Value &v) const
{
    using namespace std::placeholders;
    auto result = fetchObject(_bsdfs, v, std::bind(&Scene::instantiateBsdf, this,
            std::placeholders::_1, std::placeholders::_2));
    if (!result)
        return _errorBsdf;
    return result;
}

std::shared_ptr<Texture> Scene::fetchTexture(const rapidjson::Value &v, TexelConversion conversion) const
{
    // Note: TexelConversions are only honored by BitmapTexture.
    // This is inconsistent, but conversions do not really make sense for other textures,
    // unless the user expects e.g. a ConstantTexture with Vec3 argument to select the green
    // channel when used in a TransparencyBsdf.
    if (v.IsString())
        return _textureCache->fetchTexture(fetchResource(v), conversion);
    else if (v.IsNumber())
        return std::make_shared<ConstantTexture>(JsonUtils::as<float>(v));
    else if (v.IsArray())
        return std::make_shared<ConstantTexture>(JsonUtils::as<Vec3f>(v));
    else if (v.IsObject())
        return instantiateTexture(JsonUtils::as<std::string>(v, "type"), v, conversion);
    else
        DBG("Cannot instantiate texture from unknown value type");
    return nullptr;
}

bool Scene::textureFromJsonMember(const rapidjson::Value &v, const char *field, TexelConversion conversion,
        std::shared_ptr<Texture> &dst) const
{
    auto member = v.FindMember(field);
    if (member == v.MemberEnd())
        return false;

    std::shared_ptr<Texture> tex = fetchTexture(member->value, conversion);
    if (!tex)
        return false;

    dst = std::move(tex);
    return true;
}

PathPtr Scene::fetchResource(const std::string &path) const
{
    Path key = Path(path).normalize();

    auto iter = _resources.find(key);
    if (iter == _resources.end()) {
        std::shared_ptr<Path> resource = std::make_shared<Path>(path);
        resource->freezeWorkingDirectory();

        _resources.insert(std::make_pair(key, resource));

        return resource;
    } else {
        return iter->second;
    }
}

PathPtr Scene::fetchResource(const rapidjson::Value &v) const
{
    std::string value;
    if (!JsonUtils::fromJson(v, value))
        return nullptr;

    return fetchResource(value);
}

PathPtr Scene::fetchResource(const rapidjson::Value &v, const char *field) const
{
    auto member = v.FindMember(field);
    if (member == v.MemberEnd())
        return nullptr;
    return fetchResource(member->value);
}

const Primitive *Scene::findPrimitive(const std::string &name) const
{
    for (const std::shared_ptr<Primitive> &m : _primitives)
        if (m->name() == name)
            return m.get();
    return nullptr;
}

template<typename T>
bool Scene::addUnique(const std::shared_ptr<T> &o, std::vector<std::shared_ptr<T>> &list)
{
    bool retry = false;
    int dupeCount = 0;
    std::string baseName = o->name();
    if (baseName.empty())
        return true;
    for (int i = 1; !baseName.empty() && isdigit(baseName.back()); i *= 10, baseName.pop_back())
        dupeCount += i*(baseName.back() - '0');
    std::string newName = o->name();
    do {
        retry = false;
        for (const std::shared_ptr<T> &m : list) {
            if (m.get() == o.get())
                return false;
            if (m->name() == newName) {
                newName = tfm::format("%s%d", baseName, ++dupeCount);
                retry = true;
                break;
            }
        }
    } while (retry);

    o->setName(newName);
    list.push_back(o);

    return true;
}

void Scene::addPrimitive(const std::shared_ptr<Primitive> &mesh)
{
    if (addUnique(mesh, _primitives)) {
        for (int i = 0; i < mesh->numBsdfs(); ++i)
            addBsdf(mesh->bsdf(i));
        if (mesh->intMedium())
            addUnique(mesh->intMedium(), _media);
        if (mesh->extMedium())
            addUnique(mesh->extMedium(), _media);
    }
}

void Scene::addBsdf(const std::shared_ptr<Bsdf> &bsdf)
{
    addUnique(bsdf, _bsdfs);
}

void Scene::merge(Scene scene)
{
    for (std::shared_ptr<Primitive> &m : scene._primitives)
        addPrimitive(m);
}

void Scene::fromJson(const rapidjson::Value &v, const Scene &scene)
{
    JsonSerializable::fromJson(v, scene);

    auto media      = v.FindMember("media");
    auto bsdfs      = v.FindMember("bsdfs");
    auto primitives = v.FindMember("primitives");
    auto camera     = v.FindMember("camera");
    auto integrator = v.FindMember("integrator");
    auto renderer   = v.FindMember("renderer");

    if (media != v.MemberEnd() && media->value.IsArray())
        loadObjectList(media->value, std::bind(&Scene::instantiateMedium, this,
                std::placeholders::_1, std::placeholders::_2), _media);

    if (bsdfs != v.MemberEnd() && bsdfs->value.IsArray())
        loadObjectList(bsdfs->value, std::bind(&Scene::instantiateBsdf, this,
                std::placeholders::_1, std::placeholders::_2), _bsdfs);

    if (primitives != v.MemberEnd() && primitives->value.IsArray())
        loadObjectList(primitives->value, std::bind(&Scene::instantiatePrimitive, this,
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
}

rapidjson::Value Scene::toJson(Allocator &allocator) const
{

    rapidjson::Value media(rapidjson::kArrayType);
    for (const std::shared_ptr<Medium> &b : _media)
        media.PushBack(b->toJson(allocator), allocator);

    rapidjson::Value bsdfs(rapidjson::kArrayType);
    for (const std::shared_ptr<Bsdf> &b : _bsdfs)
        bsdfs.PushBack(b->toJson(allocator), allocator);

    rapidjson::Value primitives(rapidjson::kArrayType);
    for (const std::shared_ptr<Primitive> &t : _primitives)
        if (!_helperPrimitives.count(t.get()))
            primitives.PushBack(t->toJson(allocator), allocator);

    return JsonObject{JsonSerializable::toJson(allocator), allocator,
        "media", std::move(media),
        "bsdfs", std::move(bsdfs),
        "primitives", std::move(primitives),
        "camera", *_camera,
        "integrator", *_integrator,
        "renderer", _rendererSettings
    };
}

void Scene::loadResources()
{
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

void Scene::saveResources()
{
    for (const std::shared_ptr<Medium> &b : _media)
        b->saveResources();
    for (const std::shared_ptr<Bsdf> &b : _bsdfs)
        b->saveResources();
    for (const std::shared_ptr<Primitive> &t : _primitives)
        t->saveResources();

    _camera->saveResources();
    _integrator->saveResources();
    _rendererSettings.saveResources();
}

template<typename T>
void deleteObjects(std::vector<std::shared_ptr<T>> &dst, const std::unordered_set<T *> &objects)
{
    std::vector<std::shared_ptr<T>> newObjects;
    newObjects.reserve(dst.size());

    for (std::shared_ptr<T> &m : dst)
        if (!objects.count(m.get()))
            newObjects.push_back(m);

    dst = std::move(newObjects);
}

template<typename T>
void pruneObjects(std::vector<std::shared_ptr<T>> &dst)
{
    std::vector<std::shared_ptr<T>> newObjects;
    newObjects.reserve(dst.size());

    for (std::shared_ptr<T> &m : dst)
        if (m.use_count() > 1)
            newObjects.push_back(m);

    dst = std::move(newObjects);
}

void Scene::deletePrimitives(const std::unordered_set<Primitive *> &primitives)
{
    deleteObjects(_primitives, primitives);
}

void Scene::deleteBsdfs(const std::unordered_set<Bsdf *> &bsdfs)
{
    deleteObjects(_bsdfs, bsdfs);
}

void Scene::deleteMedia(const std::unordered_set<Medium *> &media)
{
    deleteObjects(_media, media);
}

void Scene::pruneBsdfs()
{
    pruneObjects(_bsdfs);
}

void Scene::pruneMedia()
{
    pruneObjects(_media);
}

TraceableScene *Scene::makeTraceable(uint32 seed)
{
    return new TraceableScene(*_camera, *_integrator, _primitives, _bsdfs, _media, _rendererSettings, seed);
}

Scene *Scene::load(const Path &path, std::shared_ptr<TextureCache> cache)
{
    std::string json = FileUtils::loadText(path);
    if (json.empty())
        throw std::runtime_error(tfm::format("Unable to open file at '%s'", path));

    rapidjson::Document document;
    document.Parse<0>(json.c_str());
    if (document.HasParseError())
        throw std::runtime_error(tfm::format("JSON parse error: %s", document.GetParseError()));

    DirectoryChange context(path.parent());

    if (!cache)
        cache = std::make_shared<TextureCache>();

    Scene *scene = new Scene(path.parent(), std::move(cache));
    scene->fromJson(document, *scene);
    scene->setPath(path);

    return scene;
}

void Scene::save(const Path &path, const Scene &scene)
{
    rapidjson::Document document;
    document.SetObject();

    *(static_cast<rapidjson::Value *>(&document)) = scene.toJson(document.GetAllocator());

    FileUtils::writeJson(document, path);
}

}
