#pragma once
#include "math/Vec.h"
#include <type_traits>

class Vertex
{
    Vec3f _pos, _normal;
    Vec2f _uv;

public:
    Vertex() = default;

    Vertex(const Vec3f &pos)
    : _pos(pos)
    {
    }

    Vertex(const Vec3f &pos, const Vec2f &uv)
    : _pos(pos), _uv(uv)
    {
    }

    Vertex(const Vec3f &pos, const Vec3f &normal, const Vec2f &uv)
    : _pos(pos), _normal(normal), _uv(uv)
    {
    }

    const Vec3f &normal() const
    {
        return _normal;
    }

    const Vec3f &pos() const
    {
        return _pos;
    }

    const Vec2f &uv() const
    {
        return _uv;
    }

    Vec3f &normal()
    {
        return _normal;
    }

    Vec3f &pos()
    {
        return _pos;
    }

    Vec2f &uv()
    {
        return _uv;
    }
};

static_assert(std::is_pod<Vertex>::value, "Vertex needs to be of POD type!");
