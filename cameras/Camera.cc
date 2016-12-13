#include "Camera.h"

#include "math/Angle.h"
#include "math/Ray.h"

#include "io/JsonObject.h"
#include "io/FileUtils.h"
#include "io/Scene.h"

#include <iostream>

// Default to low-res 16:9
Camera::Camera()
: Camera(Mat4f(), Vec2u(1000u, 563u))
{
}

Camera::Camera(const Mat4f &transform, const Vec2u &res)
: _tonemapString("gamma"),
  _transform(transform),
  _res(res)
{
    _colorBufferSettings.setType(OutputColor);

    _pos    = _transform*Vec3f(0.0f, 0.0f, 2.0f);
    _lookAt = _transform*Vec3f(0.0f, 0.0f, -1.0f);
    _up     = _transform*Vec3f(0.0f, 1.0f, 0.0f);

    _transform.setRight(-_transform.right());

    precompute();
}

void Camera::precompute()
{
    _ratio = _res.y()/float(_res.x());
    _pixelSize = Vec2f(1.0f/_res.x(), 1.0f/_res.y());
    _invTransform = _transform.pseudoInvert();
}

void Camera::fromJson(const rapidjson::Value &v, const Scene &scene)
{
    auto medium    = v.FindMember("medium");
    auto transform = v.FindMember("transform");

    ::fromJson(v, "tonemap", _tonemapString);
    ::fromJson(v, "resolution", _res);
    if (medium != v.MemberEnd())
        _medium = scene.fetchMedium(medium->value);

    if (transform != v.MemberEnd()) {
        ::fromJson(transform->value, _transform);
        _pos    = _transform.extractTranslationVec();
        _lookAt = _transform.fwd() + _pos;
        _up     = _transform.up();

        if (transform->value.IsObject()) {
            ::fromJson(transform->value, "up", _up);
            ::fromJson(transform->value, "look_at", _lookAt);
        }

        _transform.setRight(-_transform.right());
    }

    precompute();
}

rapidjson::Value Camera::toJson(Allocator &allocator) const
{
    JsonObject result{JsonSerializable::toJson(allocator), allocator,
        "tonemap", _tonemapString,
        "resolution", _res,
        "transform", JsonObject{allocator,
            "position", _pos,
            "look_at", _lookAt,
            "up", _up
        }
    };
    if (_medium)
        result.add("medium", *_medium);

    return result;
}

bool Camera::samplePosition(PathSampleGenerator &/*sampler*/, PositionSample &/*sample*/, Vec2u) const
{
    return false;
}

bool Camera::sampleDirection(PathSampleGenerator &/*sampler*/, const PositionSample &/*point*/,
        DirectionSample &/*sample*/) const
{
    return false;
}

bool Camera::sampleDirection(PathSampleGenerator &/*sampler*/, const PositionSample &/*point*/, Vec2u /*pixel*/,
        DirectionSample &/*sample*/) const
{
    return false;
}

bool Camera::sampleDirect(const Vec3f &/*p*/, PathSampleGenerator &/*sampler*/, LensSample &/*sample*/) const
{
    return false;
}

bool Camera::evalDirection(PathSampleGenerator &/*sampler*/, const PositionSample &/*point*/,
        const DirectionSample &/*direction*/, Vec3f &/*weight*/, Vec2f &/*pixel*/) const
{
    return false;
}

float Camera::directionPdf(const PositionSample &/*point*/, const DirectionSample &/*direction*/) const
{
    return 0.0f;
}

void Camera::prepareForRender()
{
    precompute();
}

void Camera::teardownAfterRender()
{
         _colorBuffer.reset();
         _depthBuffer.reset();
        _normalBuffer.reset();
        _albedoBuffer.reset();
    _visibilityBuffer.reset();
}

void Camera::requestOutputBuffers(const std::vector<OutputBufferSettings> &settings)
{
    for (const auto &b : settings) {
        switch (b.type()) {
        case OutputColor:           _colorBuffer.reset(new OutputBufferVec3f(_res, b)); break;
        case OutputDepth:           _depthBuffer.reset(new OutputBufferF    (_res, b)); break;
        case OutputNormal:         _normalBuffer.reset(new OutputBufferVec3f(_res, b)); break;
        case OutputAlbedo:         _albedoBuffer.reset(new OutputBufferVec3f(_res, b)); break;
        case OutputVisibility: _visibilityBuffer.reset(new OutputBufferF    (_res, b)); break;
        default: break;
        }
    }
}

void Camera::requestColorBuffer()
{
    if (!_colorBuffer)
        _colorBuffer.reset(new OutputBufferVec3f(_res, _colorBufferSettings));
    _colorBufferWeight = 1.0;
}

void Camera::setTransform(const Vec3f &pos, const Vec3f &lookAt, const Vec3f &up)
{
    _pos = pos;
    _lookAt = lookAt;
    _up = up;
    _transform = Mat4f::lookAt(_pos, _lookAt - _pos, _up);
    precompute();
}

void Camera::setPos(const Vec3f &pos)
{
    _pos = pos;
    _transform = Mat4f::lookAt(_pos, _lookAt - _pos, _up);
    precompute();
}

void Camera::setLookAt(const Vec3f &lookAt)
{
    _lookAt = lookAt;
    _transform = Mat4f::lookAt(_pos, _lookAt - _pos, _up);
    precompute();
}

void Camera::setUp(const Vec3f &up)
{
    _up = up;
    _transform = Mat4f::lookAt(_pos, _lookAt - _pos, _up);
    precompute();
}

void Camera::saveOutputBuffers() const
{
    if (     _colorBuffer)      _colorBuffer->save();
    if (     _depthBuffer)      _depthBuffer->save();
    if (    _normalBuffer)     _normalBuffer->save();
    if (    _albedoBuffer)     _albedoBuffer->save();
    if (_visibilityBuffer) _visibilityBuffer->save();
}

void Camera::serializeOutputBuffers(OutputStreamHandle &out) const
{
    if (     _colorBuffer)      _colorBuffer->serialize(out);
    if (     _depthBuffer)      _depthBuffer->serialize(out);
    if (    _normalBuffer)     _normalBuffer->serialize(out);
    if (    _albedoBuffer)     _albedoBuffer->serialize(out);
    if (_visibilityBuffer) _visibilityBuffer->serialize(out);
}

void Camera::deserializeOutputBuffers(InputStreamHandle &in)
{
    if (     _colorBuffer)      _colorBuffer->deserialize(in);
    if (     _depthBuffer)      _depthBuffer->deserialize(in);
    if (    _normalBuffer)     _normalBuffer->deserialize(in);
    if (    _albedoBuffer)     _albedoBuffer->deserialize(in);
    if (_visibilityBuffer) _visibilityBuffer->deserialize(in);
}
