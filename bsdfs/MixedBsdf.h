#include "Bsdf.h"
#include <memory>

struct IntersectionInfo;
struct Scene;

class MixedBsdf : public Bsdf
{
    std::shared_ptr<Bsdf> _bsdf0, _bsdf1;
    std::shared_ptr<Texture> _ratio;

    bool adjustedRatio(BsdfLobes requestedLobe, const IntersectionInfo *info, float &ratio) const;

public:
    MixedBsdf();
    MixedBsdf(std::shared_ptr<Bsdf> bsdf0, std::shared_ptr<Bsdf> bsdf1, float ratio);

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;

    virtual bool sample(SurfaceScatterEvent &event) const override;
    virtual Vec3f eval(const SurfaceScatterEvent &event) const override;
    virtual float pdf(const SurfaceScatterEvent &event) const override;

    virtual void prepareForRender() override;

    const std::shared_ptr<Bsdf> &bsdf0() const
    {
        return _bsdf0;
    }

    const std::shared_ptr<Bsdf> &bsdf1() const
    {
        return _bsdf1;
    }

    const std::shared_ptr<Texture> &ratio() const
    {
        return _ratio;
    }

    void setBsdf0(const std::shared_ptr<Bsdf> &bsdf0)
    {
        _bsdf0 = bsdf0;
    }

    void setBsdf1(const std::shared_ptr<Bsdf> &bsdf1)
    {
        _bsdf1 = bsdf1;
    }

    void setRatio(const std::shared_ptr<Texture> &ratio)
    {
        _ratio = ratio;
    }
};
