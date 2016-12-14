#include "Sphere.h"
#include "TriangleMesh.h"

#include "sampling/PathSampleGenerator.h"
#include "sampling/SampleWarp.h"

#include "io/JsonObject.h"
#include "io/Scene.h"

struct SphereIntersection
{
    bool backSide;
};

Sphere::Sphere()
: _pos(0.0f),
  _radius(1.0f)
{
}

Sphere::Sphere(const Vec3f &pos, float r, const std::string &name, std::shared_ptr<Bsdf> bsdf)
: Primitive(name),
  _pos(pos),
  _radius(r),
  _bsdf(std::move(bsdf))
{
    _transform = Mat4f::translate(_pos)*Mat4f::scale(Vec3f(_radius));
}

float Sphere::solidAngle(const Vec3f &p) const
{
    Vec3f L = _pos - p;
    float d = L.length();
    float cosTheta = std::sqrt(max(d*d - _radius*_radius, 0.0f))/d;

    return TWO_PI*(1.0f - cosTheta);
}

void Sphere::buildProxy()
{
    _proxy = std::make_shared<TriangleMesh>(std::vector<Vertex>(), std::vector<TriangleI>(), _bsdf, "Sphere", false, false);
    _proxy->makeSphere(1.0f);
}

float Sphere::powerToRadianceFactor() const
{
    return INV_PI*_invArea;
}

void Sphere::fromJson(const rapidjson::Value &v, const Scene &scene)
{
    Primitive::fromJson(v, scene);

    _bsdf = scene.fetchBsdf(fetchMember(v, "bsdf"));
}

bool Sphere::intersect(Ray &ray, IntersectionTemporary &data) const
{
    Vec3f p = ray.pos() - _pos;
    float B = p.dot(ray.dir());
    float C = p.lengthSq() - _radius*_radius;
    float detSq = B*B - C;
    if (detSq >= 0.0f) {
        float det = std::sqrt(detSq);
        float t = -B - det;
        if (t < ray.farT() && t > ray.nearT()) {
            ray.setFarT(t);
            data.primitive = this;
            data.as<SphereIntersection>()->backSide = false;
            return true;
        }
        t = -B + det;
        if (t < ray.farT() && t > ray.nearT()) {
            ray.setFarT(t);
            data.primitive = this;
            data.as<SphereIntersection>()->backSide = true;
            return true;
        }
    }

    return false;
}

bool Sphere::occluded(const Ray &ray) const
{
    Vec3f p = ray.pos() - _pos;
    float B = p.dot(ray.dir());
    float C = p.lengthSq() - _radius*_radius;
    float detSq = B*B - C;
    if (detSq >= 0.0f) {
        float det = std::sqrt(detSq);
        float t = -B - det;
        if (t < ray.farT() && t > ray.nearT())
            return true;
        t = -B + det;
        if (t < ray.farT() && t > ray.nearT())
            return true;
    }

    return false;
}

bool Sphere::hitBackside(const IntersectionTemporary &data) const
{
    return data.as<SphereIntersection>()->backSide;
}

void Sphere::intersectionInfo(const IntersectionTemporary &/*data*/, IntersectionInfo &info) const
{
    info.Ns = info.Ng = (info.p - _pos)/_radius;
    Vec3f localN = _invRot.transformVector(info.Ng);
    info.uv = Vec2f(std::atan2(localN.y(), localN.x())*INV_TWO_PI + 0.5f, std::acos(clamp(-1.0f, localN.z(), 1.0f))*INV_PI);
    if (std::isnan(info.uv.x()))
        info.uv.x() = 0.0f;
    info.primitive = this;
    info.bsdf = _bsdf.get();
}

bool Sphere::tangentSpace(const IntersectionTemporary &/*data*/, const IntersectionInfo &info, Vec3f &T, Vec3f &B) const
{
    Vec3f localN = _invRot.transformVector(info.Ng);
    T = _rot.transformVector(Vec3f(-localN.y(), localN.x(), localN.z()));
    B = info.Ns.cross(T);
    return true;
}

bool Sphere::isSamplable() const
{
    return true;
}

void Sphere::makeSamplable(const TraceableScene &/*scene*/, uint32 /*threadIndex*/)
{
}

bool Sphere::samplePosition(PathSampleGenerator &sampler, PositionSample &sample) const
{
    Vec2f xi = sampler.next2D();
    Vec3f localN = uniformSphere(xi);
    sample.Ng = _rot*localN;
    sample.p = sample.Ng*_radius + _pos;
    sample.pdf = _invArea;
    sample.uv = Vec2f(xi.x() + 0.5f, std::acos(clamp(-1.0f, xi.y()*2.0f - 1.0f, 1.0f))*INV_PI);
    if (sample.uv.x() > 1.0f)
        sample.uv.x() -= 1.0f;
    sample.weight = PI*_area*(*_emission)[sample.uv];

    return true;
}

bool Sphere::sampleDirection(PathSampleGenerator &sampler, const PositionSample &point, DirectionSample &sample) const
{
    Vec3f d = cosineHemisphere(sampler.next2D());
    sample.d = TangentFrame(point.Ng).toGlobal(d);
    sample.weight = Vec3f(1.0f);
    sample.pdf = cosineHemispherePdf(d);

    return true;
}

bool Sphere::sampleDirect(uint32 /*threadIndex*/, const Vec3f &p, PathSampleGenerator &sampler, LightSample &sample) const
{
    Vec3f L = _pos - p;
    float d = L.length();
    float C = d*d - _radius*_radius;
    if (C <= 0.0f)
        return false;

    L.normalize();
    float cosTheta = std::sqrt(C)/d;
    sample.d = uniformSphericalCap(sampler.next2D(), cosTheta);

    float B = d*sample.d.z();
    float det = std::sqrt(max(B*B - C, 0.0f));
    sample.dist = B - det;

    TangentFrame frame(L);
    sample.d = frame.toGlobal(sample.d);
    sample.pdf = uniformSphericalCapPdf(cosTheta);

    return true;
}

float Sphere::positionalPdf(const PositionSample &/*point*/) const
{
    return _invArea;
}

float Sphere::directionalPdf(const PositionSample &point, const DirectionSample &sample) const
{
    return max(sample.d.dot(point.Ng)*INV_PI, 0.0f);
}

float Sphere::directPdf(uint32 /*threadIndex*/, const IntersectionTemporary &/*data*/,
        const IntersectionInfo &/*info*/, const Vec3f &p) const
{
    float dist = (_pos - p).length();
    float cosTheta = std::sqrt(max(dist*dist - _radius*_radius, 0.0f))/dist;
    return uniformSphericalCapPdf(cosTheta);
}

Vec3f Sphere::evalPositionalEmission(const PositionSample &sample) const
{
    return PI*(*_emission)[sample.uv];
}

Vec3f Sphere::evalDirectionalEmission(const PositionSample &point, const DirectionSample &sample) const
{
    return Vec3f(max(sample.d.dot(point.Ng), 0.0f)*INV_PI);
}

Vec3f Sphere::evalDirect(const IntersectionTemporary &data, const IntersectionInfo &info) const
{
    return data.as<SphereIntersection>()->backSide ? Vec3f(0.0f) : (*_emission)[info.uv];
}

bool Sphere::invertParametrization(Vec2f uv, Vec3f &pos) const
{
    float phi = uv.x()*TWO_PI - PI;
    float theta = uv.y()*PI;
    Vec3f localPos = Vec3f(std::cos(phi)*std::sin(theta), std::sin(phi)*std::sin(theta), std::cos(theta));
    pos = _rot.transformVector(localPos*_radius) + _pos;
    return true;
}

bool Sphere::isDirac() const
{
    return false;
}

bool Sphere::isInfinite() const
{
    return false;
}

float Sphere::approximateRadiance(uint32 /*threadIndex*/, const Vec3f &p) const
{
    if (!isEmissive())
        return 0.0f;
    return solidAngle(p)*_emission->average().max();
}

Box3f Sphere::bounds() const
{
    return Box3f(_pos - _radius, _pos + _radius);
}

const TriangleMesh &Sphere::asTriangleMesh()
{
    if (!_proxy)
        buildProxy();
    return *_proxy;
}

void Sphere::prepareForRender()
{
    _pos = _transform*Vec3f(0.0f);
    _radius = (_transform.extractScale()*Vec3f(1.0f)).max();
    _rot = _transform.extractRotation();
    _invRot = _rot.transpose();
    _area = FOUR_PI*_radius*_radius;
    _invArea = 1.0f/_area;

    Primitive::prepareForRender();
}

int Sphere::numBsdfs() const
{
    return 1;
}

std::shared_ptr<Bsdf> &Sphere::bsdf(int /*index*/)
{
    return _bsdf;
}

void Sphere::setBsdf(int /*index*/, std::shared_ptr<Bsdf> &bsdf)
{
    _bsdf = bsdf;
}

Primitive *Sphere::clone()
{
    return new Sphere(*this);
}
