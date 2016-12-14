#pragma once
#include "ImageIO.h"
#include <functional>
#include <utility>
#include <memory>
#include <string>
#include <set>
#undef Type
#undef unused
#define RAPIDJSON_ASSERT(x) assert(x)
#include <rapidjson/document.h>
#define Type typename
#define unused __attribute((unused))

class BitmapTexture;
class IesTexture;
struct Scene;

class TextureCache
{
    typedef std::shared_ptr<BitmapTexture> BitmapKeyType;
    typedef std::shared_ptr<IesTexture> IesKeyType;

    std::set<BitmapKeyType, std::function<bool(const BitmapKeyType &, const BitmapKeyType &)>> _textures;
    std::set<IesKeyType, std::function<bool(const IesKeyType &, const IesKeyType &)>> _iesTextures;

public:
    TextureCache();

    std::shared_ptr<BitmapTexture> fetchTexture(const rapidjson::Value &value, TexelConversion conversion,
            const Scene *scene);
    std::shared_ptr<BitmapTexture> fetchTexture(PathPtr path, TexelConversion conversion,
            bool gammaCorrect = true, bool linear = true, bool clamp = false);

    std::shared_ptr<IesTexture> fetchIesTexture(const rapidjson::Value &value, const Scene *scene);
    std::shared_ptr<IesTexture> fetchIesTexture(PathPtr path, int resolution);

    void loadResources();
    void prune();
};
