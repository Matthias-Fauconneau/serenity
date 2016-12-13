#include "VdbGrid.h"

#if OPENVDB_AVAILABLE

#include "VdbRaymarcher.h"

#include "sampling/PathSampleGenerator.h"

#include "math/BitManip.h"

#include "io/JsonObject.h"
#include "io/Scene.h"

#include "Debug.h"

#include <openvdb/tools/Interpolation.h>
#include <iostream>

namespace Tungsten {

std::string VdbGrid::sampleMethodToString(SampleMethod method)
{
    switch (method) {
    default:
    case SampleMethod::ExactNearest: return "exact_nearest";
    case SampleMethod::ExactLinear:  return "exact_linear";
    case SampleMethod::Raymarching:  return "raymarching";
    }
}

std::string VdbGrid::integrationMethodToString(IntegrationMethod method)
{
    switch (method) {
    default:
    case IntegrationMethod::ExactNearest:  return "exact_nearest";
    case IntegrationMethod::ExactLinear:   return "exact_linear";
    case IntegrationMethod::Raymarching:   return "raymarching";
    case IntegrationMethod::ResidualRatio: return "residual_ratio";
    }
}

VdbGrid::SampleMethod VdbGrid::stringToSampleMethod(const std::string &name)
{
    if (name == "exact_nearest")
        return SampleMethod::ExactNearest;
    else if (name == "exact_linear")
        return SampleMethod::ExactLinear;
    else if (name == "raymarching")
        return SampleMethod::Raymarching;
    FAIL("Invalid sample method: '%s'", name);
}

VdbGrid::IntegrationMethod VdbGrid::stringToIntegrationMethod(const std::string &name)
{
    if (name == "exact_nearest")
        return IntegrationMethod::ExactNearest;
    else if (name == "exact_linear")
        return IntegrationMethod::ExactLinear;
    else if (name == "raymarching")
        return IntegrationMethod::Raymarching;
    else if (name == "residual_ratio")
        return IntegrationMethod::ResidualRatio;
    FAIL("Invalid integration method: '%s'", name);
}

VdbGrid::VdbGrid()
: _gridName("density"),
  _integrationString("exact_nearest"),
  _sampleString("exact_nearest"),
  _stepSize(5.0f),
  _supergridSubsample(10)
{
    _integrationMethod = stringToIntegrationMethod(_integrationString);
    _sampleMethod = stringToSampleMethod(_sampleString);
}

inline int roundDown(int a, int b)
{
    int c = a >> 31;
    return c ^ ((c ^ a)/b);
}

void VdbGrid::generateSuperGrid()
{
    const int offset = _supergridSubsample/2;
    auto divideCoord = [&](const openvdb::Coord &a)
    {
        return openvdb::Coord(
            roundDown(a.x() + offset, _supergridSubsample),
            roundDown(a.y() + offset, _supergridSubsample),
            roundDown(a.z() + offset, _supergridSubsample));
    };

    _superGrid = Vec2fGrid::create(openvdb::Vec2s(0.0f));
    auto accessor = _superGrid->getAccessor();

    Vec2fGrid::Ptr minMaxGrid = Vec2fGrid::create(openvdb::Vec2s(1e30f, 0.0f));
    auto minMaxAccessor = minMaxGrid->getAccessor();

    for (openvdb::FloatGrid::ValueOnCIter iter = _grid->cbeginValueOn(); iter.test(); ++iter) {
        openvdb::Coord coord = divideCoord(iter.getCoord());
        float d = *iter;
        accessor.setValue(coord, openvdb::Vec2s(accessor.getValue(coord).x() + d, 0.0f));

        openvdb::Vec2s minMax = minMaxAccessor.getValue(coord);
        minMaxAccessor.setValue(coord, openvdb::Vec2s(min(minMax.x(), d), max(minMax.y(), d)));
    }

    float normalize = 1.0f/cube(_supergridSubsample);
    const float Gamma = 2.0f;
    const float D = std::sqrt(3.0f)*_supergridSubsample;
    for (Vec2fGrid::ValueOnIter iter = _superGrid->beginValueOn(); iter.test(); ++iter) {
        openvdb::Vec2s minMax = minMaxAccessor.getValue(iter.getCoord());

        float muMin = minMax.x();
        float muMax = minMax.y();
        float muAvg = iter->x()*normalize;
        float muR = muMax - muMin;
        float muC = clamp(muMin + muR*(std::pow(Gamma, 1.0f/(D*muR)) - 1.0f), muMin, muAvg);
        iter.setValue(openvdb::Vec2s(muC, 0.0f));
    }

    for (openvdb::FloatGrid::ValueOnCIter iter = _grid->cbeginValueOn(); iter.test(); ++iter) {
        openvdb::Coord coord = divideCoord(iter.getCoord());
        openvdb::Vec2s v = accessor.getValue(coord);
        float residual = max(v.y(), std::abs(*iter - v.x()));
        accessor.setValue(coord, openvdb::Vec2s(v.x(), residual));
    }
}

void VdbGrid::fromJson(const rapidjson::Value &v, const Scene &scene)
{
    _path = scene.fetchResource(v, "file");
    JsonUtils::fromJson(v, "grid_name", _gridName);
    JsonUtils::fromJson(v, "integration_method", _integrationString);
    JsonUtils::fromJson(v, "sampling_method", _sampleString);
    JsonUtils::fromJson(v, "step_size", _stepSize);
    JsonUtils::fromJson(v, "supergrid_subsample", _supergridSubsample);
    JsonUtils::fromJson(v, "transform", _configTransform);

    _integrationMethod = stringToIntegrationMethod(_integrationString);
    _sampleMethod = stringToSampleMethod(_sampleString);
}

rapidjson::Value VdbGrid::toJson(Allocator &allocator) const
{
    JsonObject result{Grid::toJson(allocator), allocator,
        "type", "vdb",
        "file", *_path,
        "grid_name", _gridName,
        "integration_method", _integrationString,
        "sampling_method", _sampleString,
        "transform", _configTransform
    };
    if (_integrationMethod == IntegrationMethod::ResidualRatio)
        result.add("supergrid_subsample", _supergridSubsample);
    if (_integrationMethod == IntegrationMethod::Raymarching || _sampleMethod == SampleMethod::Raymarching)
        result.add("step_size", _stepSize);

    return result;
}

void VdbGrid::loadResources()
{
    openvdb::io::File file(_path->absolute().asString());
    try {
        file.open();
    } catch(const openvdb::IoError &e) {
        FAIL("Failed to open vdb file at '%s': %s", *_path, e.what());
    }

    openvdb::GridBase::Ptr ptr = file.readGrid(_gridName);
    if (!ptr)
        FAIL("Failed to read grid '%s' from vdb file '%s'", _gridName, *_path);

    file.close();

    _grid = openvdb::gridPtrCast<openvdb::FloatGrid>(ptr);
    if (!_grid)
        FAIL("Failed to read grid '%s' from vdb file '%s': Grid is not a FloatGrid", _gridName, *_path);

    openvdb::CoordBBox bbox = _grid->evalActiveVoxelBoundingBox();
    Vec3i minP = Vec3i(bbox.min().x(), bbox.min().y(), bbox.min().z());
    Vec3i maxP = Vec3i(bbox.max().x(), bbox.max().y(), bbox.max().z()) + 1;
    Vec3f diag = Vec3f(maxP - minP);
    float scale = 1.0f/diag.max();
    diag *= scale;
    Vec3f center = Vec3f(minP)*scale + Vec3f(diag.x(), 0.0f, diag.z())*0.5f;

    std::cout << minP << " -> " << maxP << std::endl;

    if (_integrationMethod == IntegrationMethod::ResidualRatio)
        generateSuperGrid();

    _transform = Mat4f::translate(-center)*Mat4f::scale(Vec3f(scale));
    _invTransform = Mat4f::scale(Vec3f(1.0f/scale))*Mat4f::translate(center);
    _bounds = Box3f(Vec3f(minP), Vec3f(maxP));

    if (_sampleMethod == SampleMethod::ExactLinear || _integrationMethod == IntegrationMethod::ExactLinear) {
        auto accessor = _grid->getAccessor();
        for (openvdb::FloatGrid::ValueOnCIter iter = _grid->cbeginValueOn(); iter.test(); ++iter) {
            if (*iter != 0.0f)
                for (int z = -1; z <= 1; ++z)
                    for (int y = -1; y <= 1; ++y)
                        for (int x = -1; x <= 1; ++x)
                            accessor.setValueOn(iter.getCoord() + openvdb::Coord(x, y, z));
            _bounds = Box3f(Vec3f(minP - 1), Vec3f(maxP + 1));
        }
    }

    _invConfigTransform = _configTransform.invert();
}

Mat4f VdbGrid::naturalTransform() const
{
    return _configTransform*_transform;
}

Mat4f VdbGrid::invNaturalTransform() const
{
    return _invTransform*_invConfigTransform;
}

Box3f VdbGrid::bounds() const
{
    return _bounds;
}

template<typename TreeT>
static inline float gridAt(TreeT &acc, Vec3f p)
{
    return openvdb::tools::BoxSampler::sample(acc, openvdb::Vec3R(p.x(), p.y(), p.z()));
}

float VdbGrid::density(Vec3f p) const
{
    return gridAt(_grid->tree(), p);
}

Vec3f VdbGrid::transmittance(PathSampleGenerator &sampler, Vec3f p, Vec3f w, float t0, float t1, Vec3f sigmaT) const
{
    auto accessor = _grid->getConstAccessor();

    if (_integrationMethod == IntegrationMethod::ExactNearest) {
        VdbRaymarcher<openvdb::FloatGrid::TreeType, 3> dda;

        float integral = 0.0f;
        dda.march(DdaRay(p + 0.5f, w), t0, t1, accessor, [&](openvdb::Coord voxel, float ta, float tb) {
            integral += accessor.getValue(voxel)*(tb - ta);
            return false;
        });
        return std::exp(-integral*sigmaT);
    } else if (_integrationMethod == IntegrationMethod::ExactLinear) {
        VdbRaymarcher<openvdb::FloatGrid::TreeType, 3> dda;

        float integral = 0.0f;
        float fa = gridAt(accessor, p + w*t0);
        dda.march(DdaRay(p, w), t0, t1, accessor, [&](openvdb::Coord /*voxel*/, float ta, float tb) {
            float fb = gridAt(accessor, p + w*tb);
            integral += (fa + fb)*0.5f*(tb - ta);
            fa = fb;
            return false;
        });
        return std::exp(-integral*sigmaT);
    } else if (_integrationMethod == IntegrationMethod::ResidualRatio) {
        VdbRaymarcher<Vec2fGrid::TreeType, 3> dda;

        float scale = _supergridSubsample;
        float invScale = 1.0f/scale;
        sigmaT *= scale;

        float sigmaTc = sigmaT.max();

        auto superAccessor =  _superGrid->getConstAccessor();

        UniformSampler &generator = sampler.uniformGenerator();

        float controlIntegral = 0.0f;
        Vec3f Tr(1.0f);
        dda.march(DdaRay(p*invScale + 0.5f, w), t0*invScale, t1*invScale, superAccessor, [&](openvdb::Coord voxel, float ta, float tb) {
            openvdb::Vec2s v = superAccessor.getValue(voxel);
            float muC = v.x();
            float muR = v.y();
            muR *= sigmaTc;

            controlIntegral += muC*(tb - ta);

            while (true) {
                ta -= BitManip::normalizedLog(generator.nextI())/muR;
                if (ta >= tb)
                    break;
                Tr *= 1.0f - sigmaT*((gridAt(accessor, p + w*ta*scale) - muC)/muR);
            }

            return false;
        });
        return std::exp(-controlIntegral*sigmaT)*Tr;
    } else {
        float ta = t0;
        float fa = gridAt(accessor, p + w*t0);
        float integral = 0.0f;
        float dT = sampler.next1D()*_stepSize;
        do {
            float tb = min(ta + dT, t1);
            float fb = gridAt(accessor, p + w*tb);
            integral += (fa + fb)*0.5f*(tb - ta);
            ta = tb;
            fa = fb;
            dT = _stepSize;
        } while (ta < t1);
        return std::exp(-integral*sigmaT);
    }
}

Vec2f VdbGrid::inverseOpticalDepth(PathSampleGenerator &sampler, Vec3f p, Vec3f w, float t0, float t1,
        float sigmaT, float xi) const
{
    auto accessor = _grid->getConstAccessor();

    if (_sampleMethod == SampleMethod::ExactNearest) {
        VdbRaymarcher<openvdb::FloatGrid::TreeType, 3> dda;

        float integral = 0.0f;
        Vec2f result(t1, 0.0f);
        bool exited = !dda.march(DdaRay(p + 0.5f, w), t0, t1, accessor, [&](openvdb::Coord voxel, float ta, float tb) {
            float v = accessor.getValue(voxel);
            float delta = v*sigmaT*(tb - ta);
            if (integral + delta >= xi) {
                result = Vec2f(ta + (tb - ta)*(xi - integral)/delta, v);
                return true;
            }
            integral += delta;
            return false;
        });
        return exited ? Vec2f(t1, integral) : result;
    } else if (_sampleMethod == SampleMethod::ExactLinear) {
        VdbRaymarcher<openvdb::FloatGrid::TreeType, 3> dda;

        float integral = 0.0f;
        float fa = gridAt(accessor, p + w*t0);
        Vec2f result(t1, 0.0f);
        bool exited = !dda.march(DdaRay(p + 0.5f, w), t0, t1, accessor, [&](openvdb::Coord /*voxel*/, float ta, float tb) {
            float fb = gridAt(accessor, p + tb*w);
            float delta = (fb + fa)*0.5f*sigmaT*(tb - ta);
            if (integral + delta >= xi) {
                float a = (fb - fa)*sigmaT;
                float b = fa*sigmaT;
                float c = (integral - xi)/(tb - ta);
                float mantissa = max(b*b - 2.0f*a*c, 0.0f);
                float x1 = (-b + std::sqrt(mantissa))/a;
                result = Vec2f(ta + (tb - ta)*x1, fa + (fb - fa)*x1);
                return true;
            }
            integral += delta;
            fa = fb;
            return false;
        });
        return exited ? Vec2f(t1, integral) : result;
    } else {
        float ta = t0;
        float fa = gridAt(accessor, p + w*t0);
        float integral = 0.0f;
        float dT = sampler.next1D()*_stepSize;
        do {
            float tb = min(ta + dT, t1);
            float fb = gridAt(accessor, p + w*tb);
            float delta = (fa + fb)*sigmaT*0.5f*(tb - ta);
            if (integral + delta >= xi) {
                float a = (fb - fa)*sigmaT;
                float b = fa*sigmaT;
                float c = (integral - xi)/(tb - ta);
                float mantissa = max(b*b - 2.0f*a*c, 0.0f);
                float x1 = (-b + std::sqrt(mantissa))/a;
                return Vec2f(ta + (tb - ta)*x1, fa + (fb - fa)*x1);
            }
            integral += delta;
            ta = tb;
            fa = fb;
            dT = _stepSize;
        } while (ta < t1);
        return Vec2f(t1, integral);
    }
}

}

#endif
