#pragma once
#include "math/Vec.h"
#include "io/JsonSerializable.h"

struct IntersectionInfo;

enum TextureMapJacobian {
    MAP_UNIFORM,
    MAP_SPHERICAL,
    MAP_JACOBIAN_COUNT,
};

struct Texture : public JsonSerializable
{
    static bool scalarOrVecFromJson(const rapidjson::Value &v, const char *field, Vec3f &dst);

    virtual ~Texture() {}

    virtual bool isConstant() const = 0;

    virtual Vec3f average() const = 0;
    virtual Vec3f minimum() const = 0;
    virtual Vec3f maximum() const = 0;

    virtual Vec3f operator[](const Vec2f &uv) const = 0;
    virtual Vec3f operator[](const IntersectionInfo &info) const = 0;
    virtual void derivatives(const Vec2f &uv, Vec2f &derivs) const = 0;

    virtual void makeSamplable(TextureMapJacobian jacobian) = 0;
    virtual Vec2f sample(TextureMapJacobian jacobian, const Vec2f &uv) const = 0;
    virtual float pdf(TextureMapJacobian jacobian, const Vec2f &uv) const = 0;

    virtual void scaleValues(float factor) = 0;

    virtual Texture *clone() const = 0;
};
