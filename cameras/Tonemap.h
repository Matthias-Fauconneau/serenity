#pragma once
#include "math/MathUtil.h"
#include "math/Vec.h"
#include "Debug.h"

class Tonemap
{
public:
    enum Type {
        LinearOnly,
        GammaOnly,
        Reinhard,
        Filmic
    };

    static Type stringToType(const std::string &s)
    {
        if (s == "linear")
            return LinearOnly;
        else if (s == "gamma")
            return GammaOnly;
        else if (s == "reinhard")
            return Reinhard;
        else if (s == "filmic")
            return Filmic;
        FAIL("Invalid tonemap operator: '%s'", s.c_str());
        return GammaOnly;
    }

    static inline Vec3f tonemap(Type type, const Vec3f &c)
    {
        switch (type) {
        case LinearOnly:
            return c;
        case GammaOnly:
            return std::pow(c, 1.0f/2.2f);
        case Reinhard:
            return std::pow(c/(c + 1.0f), 1.0f/2.2f);
        case Filmic: {
            Vec3f x = max(Vec3f(0.0f), c - 0.004f);
            return (x*(6.2f*x + 0.5f))/(x*(6.2f*x + 1.7f) + 0.06f);
        }}
        return c;
    }
};
