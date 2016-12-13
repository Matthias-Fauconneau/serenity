#include "PathTracerSettings.h"
#include "SampleRecord.h"
#include "PathTracer.h"
#include "integrators/Integrator.h"
#include "integrators/ImageTile.h"
#include "sampling/PathSampleGenerator.h"
#include "sampling/UniformSampler.h"
#include "math/MathUtil.h"
#include <thread>
#include <memory>
#include <vector>
#include <atomic>

struct PathTraceIntegrator : public Integrator
{
    static constexpr uint32 TileSize = 16;
    static constexpr uint32 VarianceTileSize = 4;
    static constexpr uint32 AdaptiveThreshold = 16;

    PathTracerSettings _settings;

    uint32 _w;
    uint32 _h;
    uint32 _varianceW;
    uint32 _varianceH;

    UniformSampler _sampler;
    std::vector<std::unique_ptr<PathTracer>> _tracers;

    std::vector<SampleRecord> _samples;
    std::vector<ImageTile> _tiles;

public:
    PathTraceIntegrator();

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;
    virtual rapidjson::Value toJson(Allocator &allocator) const override;

    virtual void prepareForRender(TraceableScene &scene, uint32 seed) override;
    virtual void teardownAfterRender() override;
    
    const PathTracerSettings &settings() const
    {
        return _settings;
    }
};
