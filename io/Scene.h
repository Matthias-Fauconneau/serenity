#pragma once
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <vector>
#include <map>
#include "JsonSerializable.h"
#include "TextureCache.h"
#include "ImageIO.h"
#include "Path.h"
#include "phasefunctions/PhaseFunction.h"
#include "integrators/Integrator.h"
#include "primitives/Primitive.h"
#include "materials/BitmapTexture.h"
#include "renderer/RendererSettings.h"
#include "renderer/TraceableScene.h"
#include "cameras/Camera.h"
#include "media/Medium.h"
#include "grids/Grid.h"
#include "bsdfs/Bsdf.h"
#undef Type
#undef unused
#include <rapidjson/document.h>
#define Type typename
#define unused __attribute((unused))

struct Scene : public JsonSerializable {
    Path _srcDir;
    Path _path;

    std::vector<std::shared_ptr<Primitive>> _primitives;
    std::vector<std::shared_ptr<Medium>> _media;
    std::vector<std::shared_ptr<Bsdf>> _bsdfs;
    std::shared_ptr<Bsdf> _errorBsdf;
    std::shared_ptr<Texture> _errorTexture;
    std::shared_ptr<TextureCache> _textureCache;
    std::shared_ptr<Camera> _camera;
    std::shared_ptr<Integrator> _integrator;

    std::unordered_set<const Primitive *> _helperPrimitives;
    mutable std::unordered_map<Path, PathPtr> _resources;

    RendererSettings _rendererSettings;

    std::shared_ptr<PhaseFunction> instantiatePhase     (std::string type, const rapidjson::Value &value) const;
    std::shared_ptr<Medium>        instantiateMedium    (std::string type, const rapidjson::Value &value) const;
    std::shared_ptr<Grid>          instantiateGrid      (std::string type, const rapidjson::Value &value) const;
    std::shared_ptr<Bsdf>          instantiateBsdf      (std::string type, const rapidjson::Value &value) const;
    std::shared_ptr<Primitive>     instantiatePrimitive (std::string type, const rapidjson::Value &value) const;
    std::shared_ptr<Camera>        instantiateCamera    (std::string type, const rapidjson::Value &value) const;
    std::shared_ptr<Integrator>    instantiateIntegrator(std::string type, const rapidjson::Value &value) const;
    std::shared_ptr<Texture>       instantiateTexture   (std::string type, const rapidjson::Value &value, TexelConversion conversion) const;

    /*template<typename Instantiator, typename Element>
    void loadObjectList(const rapidjson::Value &container, Instantiator instantiator, std::vector<std::shared_ptr<Element>> &result);*/

    template<typename Instantiator, typename Element>
    void loadObjectList(const rapidjson::Value &container, Instantiator instantiator, std::vector<std::shared_ptr<Element>> &result)
    {
        for (unsigned i = 0; i < container.Size(); ++i) {
            if (container[i].IsObject()) {
                auto element = instantiator(as<std::string>(container[i], "type"), container[i]);
                if (element)
                    result.push_back(std::move(element));
            } else {
                DBG("Don't know what to do with non-object in object list");
            }
        }
    }

    template<typename T>
    std::shared_ptr<T> findObject(const std::vector<std::shared_ptr<T>> &list, std::string name) const;

    template<typename T, typename Instantiator>
    std::shared_ptr<T> fetchObject(const std::vector<std::shared_ptr<T>> &list, const rapidjson::Value &v, Instantiator instantiator) const;

    template<typename T>
    bool addUnique(const std::shared_ptr<T> &o, std::vector<std::shared_ptr<T>> &list);

public:
    Scene();

    Scene(const Path &srcDir, std::shared_ptr<TextureCache> cache);

    Scene(const Path &srcDir,
          std::vector<std::shared_ptr<Primitive>> primitives,
          std::vector<std::shared_ptr<Bsdf>> bsdfs,
          std::shared_ptr<TextureCache> cache,
          std::shared_ptr<Camera> camera);

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;
    virtual rapidjson::Value toJson(Allocator &allocator) const override;

    virtual void loadResources() override;
    virtual void saveResources() override;

    std::shared_ptr<PhaseFunction> fetchPhase(const rapidjson::Value &v) const;
    std::shared_ptr<Medium> fetchMedium(const rapidjson::Value &v) const;
    std::shared_ptr<Grid> fetchGrid(const rapidjson::Value &v) const;
    std::shared_ptr<Bsdf> fetchBsdf(const rapidjson::Value &v) const;
    std::shared_ptr<Texture> fetchTexture(const rapidjson::Value &v, TexelConversion conversion) const;
    bool textureFromJsonMember(const rapidjson::Value &v, const char *field, TexelConversion conversion,
            std::shared_ptr<Texture> &dst) const;
    PathPtr fetchResource(const std::string &path) const;
    PathPtr fetchResource(const rapidjson::Value &v) const;
    PathPtr fetchResource(const rapidjson::Value &v, const char *field) const;

    const Primitive *findPrimitive(const std::string &name) const;

    void deletePrimitives(const std::unordered_set<Primitive *> &primitives);
    void deleteBsdfs(const std::unordered_set<Bsdf *> &bsdfs);
    void deleteMedia(const std::unordered_set<Medium *> &media);

    void pruneBsdfs();
    void pruneMedia();

    void addPrimitive(const std::shared_ptr<Primitive> &mesh);
    void addBsdf(const std::shared_ptr<Bsdf> &bsdf);

    void merge(Scene scene);

    TraceableScene *makeTraceable(uint32 seed = 0xBA5EBA11);

    std::vector<std::shared_ptr<Medium>> &media()
    {
        return _media;
    }

    std::vector<std::shared_ptr<Bsdf>> &bsdfs()
    {
        return _bsdfs;
    }

    const std::vector<std::shared_ptr<Primitive>> &primitives() const
    {
        return _primitives;
    }

    std::vector<std::shared_ptr<Primitive>> &primitives()
    {
        return _primitives;
    }

    void setCamera(Camera *cam)
    {
        _camera.reset(cam);
    }

    std::shared_ptr<Camera> camera()
    {
        return _camera;
    }

    const std::shared_ptr<TextureCache> textureCache() const
    {
        return _textureCache;
    }

    const std::shared_ptr<Camera> camera() const
    {
        return _camera;
    }

    void setPath(const Path &p)
    {
        _path = p;
    }

    const Path &path() const
    {
        return _path;
    }

    const RendererSettings &rendererSettings() const
    {
        return _rendererSettings;
    }

    RendererSettings &rendererSettings()
    {
        return _rendererSettings;
    }
    
    Integrator *integrator()
    {
        return _integrator.get();
    }

    std::unordered_map<Path, PathPtr> &resources()
    {
        return _resources;
    }

    std::shared_ptr<Bsdf> errorBsdf() const
    {
        return _errorBsdf;
    }

    static Scene *load(const Path &path, std::shared_ptr<TextureCache> cache = nullptr);
    static void save(const Path &path, const Scene &scene);
};
