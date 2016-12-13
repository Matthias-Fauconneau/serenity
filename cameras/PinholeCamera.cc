#include "PinholeCamera.h"
#include "sampling/PathSampleGenerator.h"
#include "math/Angle.h"
#include "math/Ray.h"
#include "io/JsonObject.h"
#include <cmath>

PinholeCamera::PinholeCamera()
: Camera(),
  _fovDeg(60.0f)
{
    precompute();
}

PinholeCamera::PinholeCamera(const Mat4f &transform, const Vec2u &res, float fov)
: Camera(transform, res),
  _fovDeg(fov)
{
    precompute();
}

void PinholeCamera::precompute()
{
    _fovRad = Angle::degToRad(_fovDeg);
    _planeDist = 1.0f/std::tan(_fovRad*0.5f);

    float planeArea = (2.0f/_planeDist)*(2.0f*_ratio/_planeDist);
    _invPlaneArea = 1.0f/planeArea;
}

void PinholeCamera::fromJson(const rapidjson::Value &v, const Scene &scene)
{
    Camera::fromJson(v, scene);
    ::fromJson(v, "fov", _fovDeg);

    precompute();
}

rapidjson::Value PinholeCamera::toJson(Allocator &allocator) const
{
    return JsonObject{Camera::toJson(allocator), allocator,
        "type", "pinhole",
        "fov", _fovDeg
    };
}

bool PinholeCamera::samplePosition(PathSampleGenerator &/*sampler*/, PositionSample &sample, Vec2u pixel) const
{
    //sample.p = 0; //_pos;
    const vec3 O = M.inverse() * vec3(2.f*pixel.x()/float(_res.x()-1)-1, -(2.f*pixel.y()/float(_res.y()-1)-1), -1);
    sample.p.x() = O.x;
    sample.p.y() = O.y;
    sample.p.z() = O.z;
    sample.weight = Vec3f(1.0f);
    sample.pdf = 1.0f;
    sample.Ng = _transform.fwd();

    return true;
}

float PinholeCamera::directionPdf(const PositionSample &/*point*/, const DirectionSample &direction) const
{
    Vec3f localD = _invTransform.transformVector(direction.d);
    if (localD.z() <= 0.0f)
        return 0.0f;
    localD *= _planeDist/localD.z();

    float u = (localD.x() + 1.0f)*0.5f;
    float v = (1.0f - localD.y()/_ratio)*0.5f;
    if (u < 0.0f || v < 0.0f || u > 1.0f || v > 1.0f)
        return 0.0f;

    return  _invPlaneArea/cube(localD.z()/localD.length());
}

bool PinholeCamera::isDirac() const
{
    return true;
}
