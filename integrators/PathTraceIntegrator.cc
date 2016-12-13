#include "PathTraceIntegrator.h"

#include "sampling/UniformPathSampler.h"

#include "cameras/Camera.h"

constexpr uint32 PathTraceIntegrator::TileSize;
constexpr uint32 PathTraceIntegrator::VarianceTileSize;
constexpr uint32 PathTraceIntegrator::AdaptiveThreshold;

PathTraceIntegrator::PathTraceIntegrator()
: Integrator(),
  _w(0),
  _h(0),
  _varianceW(0),
  _varianceH(0),
  _sampler(0xBA5EBA11)
{
}

void PathTraceIntegrator::fromJson(const rapidjson::Value &v, const Scene &/*scene*/)
{
    _settings.fromJson(v);
}

rapidjson::Value PathTraceIntegrator::toJson(Allocator &allocator) const
{
    return _settings.toJson(allocator);
}

void PathTraceIntegrator::prepareForRender(TraceableScene &scene, uint32 seed)
{
    _sampler = UniformSampler(MathUtil::hash32(seed));
    _scene = &scene;
    scene.cam().requestColorBuffer();

    /*for (uint32 i = 0; i < ThreadUtils::pool->threadCount(); ++i)
        _tracers.emplace_back(new PathTracer(&scene, _settings, i));*/

    _w = scene.cam().resolution().x();
    _h = scene.cam().resolution().y();
    _varianceW = (_w + VarianceTileSize - 1)/VarianceTileSize;
    _varianceH = (_h + VarianceTileSize - 1)/VarianceTileSize;
    _samples.resize(_varianceW*_varianceH);
}

void PathTraceIntegrator::teardownAfterRender()
{
    _tracers.clear();
    _samples.clear();
    _tiles  .clear();
    _tracers.shrink_to_fit();
    _samples.shrink_to_fit();
    _tiles  .shrink_to_fit();
}
