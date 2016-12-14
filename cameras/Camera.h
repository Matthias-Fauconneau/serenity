#pragma once
#include "samplerecords/DirectionSample.h"
#include "samplerecords/PositionSample.h"
#include "samplerecords/LensSample.h"
#include "sampling/PathSampleGenerator.h"
#include "math/Mat4f.h"
#include "math/Vec.h"
#include "io/JsonSerializable.h"
#include "io/Path.h"
#include <vector>
#include <memory>
#include "matrix.h"
#undef Type
#undef unused
#define RAPIDJSON_ASSERT assert
#include <rapidjson/document.h>
#define Type typename
#define unused __attribute((unused))

class Ray;
struct Scene;
class Medium;
class Renderer;
class SampleGenerator;

struct Camera : public JsonSerializable
{
    mat4 M;

    std::string _tonemapString;

    Mat4f _transform;
    Mat4f _invTransform;
    Vec3f _pos;
    Vec3f _lookAt;
    Vec3f _up;

    Vec2u _res;
    float _ratio;
    Vec2f _pixelSize;

    std::shared_ptr<Medium> _medium;

    double _colorBufferWeight;

    double _splatWeight;

private:
    void precompute();

public:
    Camera();
    Camera(const Mat4f &transform, const Vec2u &res);

    void fromJson(const rapidjson::Value &v, const Scene &scene) override;

    virtual bool samplePosition(PathSampleGenerator &sampler, PositionSample &sample, Vec2u pixel) const;
    virtual bool sampleDirection(PathSampleGenerator &sampler, const PositionSample &point, DirectionSample &sample) const;
    virtual bool sampleDirection(PathSampleGenerator &sampler, const PositionSample &point, Vec2u pixel,
            DirectionSample &sample) const;
    virtual bool sampleDirect(const Vec3f &p, PathSampleGenerator &sampler, LensSample &sample) const;
    virtual bool evalDirection(PathSampleGenerator &sampler, const PositionSample &point,
            const DirectionSample &direction, Vec3f &weight, Vec2f &pixel) const;
    virtual float directionPdf(const PositionSample &point, const DirectionSample &direction) const;

    virtual bool isDirac() const = 0;

    virtual float approximateFov() const = 0;

    virtual void prepareForRender();

    void requestColorBuffer();
    void requestSplatBuffer();
    void blitSplatBuffer();

    void setTransform(const Vec3f &pos, const Vec3f &lookAt, const Vec3f &up);
    void setPos(const Vec3f &pos);
    void setLookAt(const Vec3f &lookAt);
    void setUp(const Vec3f &up);

    const Mat4f &transform() const
    {
        return _transform;
    }

    const Vec3f &pos() const
    {
        return _pos;
    }

    const Vec3f &lookAt() const
    {
        return _lookAt;
    }

    const Vec3f &up() const
    {
        return _up;
    }

    const Vec2u &resolution() const
    {
        return _res;
    }

    void setResolution(Vec2u res)
    {
        _res = res;
    }

    const std::shared_ptr<Medium> &medium() const
    {
        return _medium;
    }

    void setTonemapString(const std::string &name)
    {
        _tonemapString = name;
    }
};
