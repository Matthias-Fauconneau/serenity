#include "InfiniteSphere.h"
#include "TriangleMesh.h"
#include "TraceableScene.h"
#include "sampling/PathSampleGenerator.h"
#include "sampling/SampleWarp.h"

#include "bsdfs/NullBsdf.h"

#include "math/Angle.h"

#include "io/JsonObject.h"
#include "io/Scene.h"

struct InfiniteSphereIntersection
{
    Vec3f p;
    Vec3f w;
};

InfiniteSphere::InfiniteSphere()
: _doSample(true)
{
}

Vec2f InfiniteSphere::directionToUV(const Vec3f &wi) const
{
    Vec3f wLocal = _invRotTransform*wi;
    return Vec2f(std::atan2(wLocal.z(), wLocal.x())*INV_TWO_PI + 0.5f, std::acos(-wLocal.y())*INV_PI);
}

Vec2f InfiniteSphere::directionToUV(const Vec3f &wi, float &sinTheta) const
{
    Vec3f wLocal = _invRotTransform*wi;
    sinTheta = std::sqrt(max(1.0f - wLocal.y()*wLocal.y(), 0.0f));
    return Vec2f(std::atan2(wLocal.z(), wLocal.x())*INV_TWO_PI + 0.5f, std::acos(-wLocal.y())*INV_PI);
}

Vec3f InfiniteSphere::uvToDirection(Vec2f uv, float &sinTheta) const
{
    float phi   = (uv.x() - 0.5f)*TWO_PI;
    float theta = uv.y()*PI;
    sinTheta = std::sin(theta);
    return _rotTransform*Vec3f(
        std::cos(phi)*sinTheta,
        -std::cos(theta),
        std::sin(phi)*sinTheta
    );
}

void InfiniteSphere::buildProxy()
{
    _proxy = std::make_shared<TriangleMesh>(std::vector<Vertex>(), std::vector<TriangleI>(),
            std::make_shared<NullBsdf>(), "Sphere", false, false);
    _proxy->makeSphere(0.05f);
}

float InfiniteSphere::powerToRadianceFactor() const
{
    return INV_FOUR_PI;
}

void InfiniteSphere::fromJson(const rapidjson::Value &v, const Scene &scene)
{
    Primitive::fromJson(v, scene);
    ::fromJson(v, "sample", _doSample);
}
rapidjson::Value InfiniteSphere::toJson(Allocator &allocator) const
{
    return JsonObject{Primitive::toJson(allocator), allocator,
        "type", "infinite_sphere",
        "sample", _doSample
    };
}

bool InfiniteSphere::intersect(Ray &ray, IntersectionTemporary &data) const
{
    InfiniteSphereIntersection *isect = data.as<InfiniteSphereIntersection>();
    isect->p = ray.pos();
    isect->w = ray.dir();
    data.primitive = this;
    return true;
}

bool InfiniteSphere::occluded(const Ray &/*ray*/) const
{
    return true;
}

bool InfiniteSphere::hitBackside(const IntersectionTemporary &/*data*/) const
{
    return false;
}

void InfiniteSphere::intersectionInfo(const IntersectionTemporary &data, IntersectionInfo &info) const
{
    const InfiniteSphereIntersection *isect = data.as<InfiniteSphereIntersection>();
    info.Ng = info.Ns = -isect->w;
    info.p = isect->p;
    info.uv = directionToUV(isect->w);
    info.primitive = this;
    info.bsdf = nullptr;
}

bool InfiniteSphere::tangentSpace(const IntersectionTemporary &/*data*/, const IntersectionInfo &/*info*/,
        Vec3f &/*T*/, Vec3f &/*B*/) const
{
    return false;
}

bool InfiniteSphere::isSamplable() const
{
    return _doSample;
}

void InfiniteSphere::makeSamplable(const TraceableScene &scene, uint32 /*threadIndex*/)
{
    _emission->makeSamplable(MAP_SPHERICAL);
    _sceneBounds = scene.bounds();
    _sceneBounds.grow(1e-2f);
}

bool InfiniteSphere::samplePosition(PathSampleGenerator &sampler, PositionSample &sample) const
{
    float faceXi = sampler.next1D();
    Vec2f xi = sampler.next2D();

    if (_emission->isConstant()) {
        sample.Ng = -uniformSphere(sampler.next2D());
        sample.uv = directionToUV(-sample.Ng);
    } else {
        sample.uv = _emission->sample(MAP_SPHERICAL, sampler.next2D());
        float sinTheta;
        sample.Ng = -uvToDirection(sample.uv, sinTheta);
    }

    sample.p = projectedBox(_sceneBounds, sample.Ng, faceXi, xi);
    sample.pdf = projectedBoxPdf(_sceneBounds, sample.Ng);
    sample.weight = Vec3f(1.0f/sample.pdf);

    return true;
}

bool InfiniteSphere::sampleDirection(PathSampleGenerator &/*sampler*/, const PositionSample &point, DirectionSample &sample) const
{
    sample.d = point.Ng;
    if (_emission->isConstant()) {
        sample.pdf = INV_FOUR_PI;
    } else {
        float sinTheta;
        directionToUV(-point.Ng, sinTheta);
        sample.pdf = INV_PI*INV_TWO_PI*_emission->pdf(MAP_SPHERICAL, point.uv)/sinTheta;
        if (sample.pdf == 0.0f)
            return false;
    }
    sample.weight = (*_emission)[point.uv]/sample.pdf;

    return true;
}

bool InfiniteSphere::sampleDirect(uint32 /*threadIndex*/, const Vec3f &/*p*/, PathSampleGenerator &sampler, LightSample &sample) const
{
    if (_emission->isConstant()) {
        sample.d = uniformSphere(sampler.next2D());
        sample.dist = Ray::infinity();
        sample.pdf = INV_FOUR_PI;
        return true;
    } else {
        Vec2f uv = _emission->sample(MAP_SPHERICAL, sampler.next2D());
        float sinTheta;
        sample.d = uvToDirection(uv, sinTheta);
        sample.pdf = INV_PI*INV_TWO_PI*_emission->pdf(MAP_SPHERICAL, uv)/sinTheta;
        sample.dist = Ray::infinity();
        return sample.pdf != 0.0f;
    }
}

float InfiniteSphere::positionalPdf(const PositionSample &point) const
{
    return projectedBoxPdf(_sceneBounds, point.Ng);
}

float InfiniteSphere::directionalPdf(const PositionSample &point, const DirectionSample &/*sample*/) const
{
    if (_emission->isConstant()) {
        return INV_FOUR_PI;
    } else {
        float sinTheta;
        directionToUV(-point.Ng, sinTheta);
        return INV_PI*INV_TWO_PI*_emission->pdf(MAP_SPHERICAL, point.uv)/sinTheta;
    }
}

float InfiniteSphere::directPdf(uint32 /*threadIndex*/, const IntersectionTemporary &data,
        const IntersectionInfo &/*info*/, const Vec3f &/*p*/) const
{
    if (_emission->isConstant()) {
        return INV_FOUR_PI;
    } else {
        const InfiniteSphereIntersection *isect = data.as<InfiniteSphereIntersection>();
        float sinTheta;
        Vec2f uv = directionToUV(isect->w, sinTheta);
        return INV_PI*INV_TWO_PI*_emission->pdf(MAP_SPHERICAL, uv)/sinTheta;
    }
}

Vec3f InfiniteSphere::evalPositionalEmission(const PositionSample &/*sample*/) const
{
    return Vec3f(1.0f);
}

Vec3f InfiniteSphere::evalDirectionalEmission(const PositionSample &point, const DirectionSample &/*sample*/) const
{
    return (*_emission)[point.uv];
}

Vec3f InfiniteSphere::evalDirect(const IntersectionTemporary &/*data*/, const IntersectionInfo &info) const
{
    return (*_emission)[info.uv];
}

bool InfiniteSphere::invertParametrization(Vec2f /*uv*/, Vec3f &/*pos*/) const
{
    return false;
}

bool InfiniteSphere::isDirac() const
{
    return false;
}

bool InfiniteSphere::isInfinite() const
{
    return true;
}

float InfiniteSphere::approximateRadiance(uint32 /*threadIndex*/, const Vec3f &/*p*/) const
{
    if (!isEmissive() || !isSamplable())
        return 0.0f;
    return TWO_PI*_emission->average().max();
}

Box3f InfiniteSphere::bounds() const
{
    return Box3f(Vec3f(-1e30f), Vec3f(1e30f));
}

const TriangleMesh &InfiniteSphere::asTriangleMesh()
{
    if (!_proxy)
        buildProxy();
    return *_proxy;
}

void InfiniteSphere::prepareForRender()
{
    _rotTransform = _transform.extractRotation();
    _invRotTransform = _rotTransform.transpose();

    Primitive::prepareForRender();
}

int InfiniteSphere::numBsdfs() const
{
    return 0;
}

std::shared_ptr<Bsdf> &InfiniteSphere::bsdf(int /*index*/)
{
    FAIL("InfiniteSphere::bsdf should not be called");
}

void InfiniteSphere::setBsdf(int /*index*/, std::shared_ptr<Bsdf> &/*bsdf*/)
{
}

Primitive *InfiniteSphere::clone()
{
    return new InfiniteSphere(*this);
}
