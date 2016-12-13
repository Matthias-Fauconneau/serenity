#include "samplerecords/PhaseSample.h"
#include "io/JsonSerializable.h"

class PathSampleGenerator;

class PhaseFunction : public JsonSerializable
{
public:
    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;
    virtual rapidjson::Value toJson(Allocator &allocator) const override;

    virtual Vec3f eval(const Vec3f &wi, const Vec3f &wo) const = 0;
    virtual bool sample(PathSampleGenerator &sampler, const Vec3f &wi, PhaseSample &sample) const = 0;
    virtual float pdf(const Vec3f &wi, const Vec3f &wo) const = 0;
};
