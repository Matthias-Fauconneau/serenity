#pragma once
#include "IntersectionTemporary.h"
#include "IntersectionInfo.h"

#include "samplerecords/DirectionSample.h"
#include "samplerecords/PositionSample.h"
#include "samplerecords/LightSample.h"

#include "materials/Texture.h"

#include "bsdfs/Bsdf.h"

#include "math/TangentFrame.h"
#include "math/Mat4f.h"
#include "math/Ray.h"
#include "math/Box.h"

#include "io/JsonSerializable.h"
#include "io/JsonUtils.h"

#include <vector>
#include <memory>


struct TraceableScene;
class TriangleMesh;

class Primitive : public JsonSerializable
{
    std::shared_ptr<Medium> _intMedium;
    std::shared_ptr<Medium> _extMedium;

protected:
    std::shared_ptr<Texture> _emission;
    std::shared_ptr<Texture> _power;

    Mat4f _transform;

    bool _needsRayTransform = false;

    virtual float powerToRadianceFactor() const
    {
        return 0.0f;
    }

public:
    virtual ~Primitive() {}

    Primitive();
    Primitive(const std::string &name);

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;

    virtual bool intersect(Ray &ray, IntersectionTemporary &data) const = 0;
    virtual bool occluded(const Ray &ray) const = 0;
    virtual bool hitBackside(const IntersectionTemporary &data) const = 0;
    virtual void intersectionInfo(const IntersectionTemporary &data, IntersectionInfo &info) const = 0;
    virtual bool tangentSpace(const IntersectionTemporary &data, const IntersectionInfo &info,
            Vec3f &T, Vec3f &B) const = 0;

    virtual bool isSamplable() const = 0;
    virtual void makeSamplable(const TraceableScene &scene, uint32 threadIndex) = 0;

    virtual bool samplePosition(PathSampleGenerator &sampler, PositionSample &sample) const;
    virtual bool sampleDirection(PathSampleGenerator &sampler, const PositionSample &point, DirectionSample &sample) const;
    virtual bool sampleDirect(uint32 threadIndex, const Vec3f &p, PathSampleGenerator &sampler, LightSample &sample) const;
    virtual float positionalPdf(const PositionSample &point) const;
    virtual float directionalPdf(const PositionSample &point, const DirectionSample &sample) const;
    virtual float directPdf(uint32 threadIndex, const IntersectionTemporary &data,
            const IntersectionInfo &info, const Vec3f &p) const;
    virtual Vec3f evalPositionalEmission(const PositionSample &sample) const;
    virtual Vec3f evalDirectionalEmission(const PositionSample &point, const DirectionSample &sample) const;
    virtual Vec3f evalDirect(const IntersectionTemporary &data, const IntersectionInfo &info) const;

    virtual bool invertParametrization(Vec2f uv, Vec3f &pos) const = 0;

    virtual bool isDirac() const = 0;
    virtual bool isInfinite() const = 0;

    virtual float approximateRadiance(uint32 threadIndex, const Vec3f &p) const = 0;

    virtual Box3f bounds() const = 0;

    virtual const TriangleMesh &asTriangleMesh() = 0;

    virtual void prepareForRender();
    virtual void teardownAfterRender();

    virtual int numBsdfs() const = 0;
    virtual std::shared_ptr<Bsdf> &bsdf(int index) = 0;
    virtual void setBsdf(int index, std::shared_ptr<Bsdf> &bsdf) = 0;

    virtual Primitive *clone() = 0;

    void setupTangentFrame(const IntersectionTemporary &data,
            const IntersectionInfo &info, TangentFrame &dst) const;

    virtual std::vector<std::shared_ptr<Primitive>> createHelperPrimitives()
    {
        return std::vector<std::shared_ptr<Primitive>>();
    }

    virtual bool isEmissive() const
    {
        return (_emission.operator bool() && _emission->maximum().max() > 0.0f) ||
               (   _power.operator bool() &&    _power->maximum().max() > 0.0f);
    }

    void setEmission(const std::shared_ptr<Texture> &emission)
    {
        _emission = emission;
    }

    const std::shared_ptr<Texture> &emission() const
    {
        return _emission;
    }

    bool needsRayTransform() const
    {
        return _needsRayTransform;
    }

    void setTransform(const Mat4f &m)
    {
        _transform = m;
    }

    const Mat4f &transform() const
    {
        return _transform;
    }

    const std::shared_ptr<Medium> &extMedium() const
    {
        return _extMedium;
    }

    const std::shared_ptr<Medium> &intMedium() const
    {
        return _intMedium;
    }

    std::shared_ptr<Medium> &extMedium()
    {
        return _extMedium;
    }

    std::shared_ptr<Medium> &intMedium()
    {
        return _intMedium;
    }

    void setIntMedium(std::shared_ptr<Medium> &intMedium)
    {
        _intMedium = intMedium;
    }

    void setExtMedium(std::shared_ptr<Medium> &extMedium)
    {
        _extMedium = extMedium;
    }

    bool overridesMedia() const
    {
        return _extMedium || _intMedium;
    }

    const Medium *selectMedium(const Medium *currentMedium, bool geometricBackside) const
    {
        if (overridesMedia())
            return geometricBackside ? _intMedium.get() : _extMedium.get();
        else
            return currentMedium;
    }
};
