#pragma once
#include "core.h"
#include <string>
#undef Type
#undef unused
#define RAPIDJSON_ASSERT assert
#include <rapidjson/document.h>
#define Type typename
#define unused __attribute((unused))

struct Scene;

struct JsonSerializable {
    std::string _name;

    virtual ~JsonSerializable() {}
    typedef rapidjson::Document::AllocatorType Allocator;

    JsonSerializable() = default;
    JsonSerializable(const std::string &name);

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene);

    // Loads any additional resources referenced by this object, e.g. bitmaps
    // for textures, or mesh files in the TriangleMesh. This is split from
    // fromJson to allow parsing a scene document without loading any of the
    // heavy binary data
    virtual void loadResources() {}

    void setName(const std::string &name)
    {
        _name = name;
    }

    const std::string &name() const
    {
        return _name;
    }

    bool unnamed() const
    {
        return _name.empty();
    }
};
