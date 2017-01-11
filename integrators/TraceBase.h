#include "time.h"
#include "TraceSettings.h"
#include "samplerecords/SurfaceScatterEvent.h"
#include "samplerecords/MediumSample.h"
#include "samplerecords/LightSample.h"
#include "sampling/PathSampleGenerator.h"
#include "sampling/UniformPathSampler.h"
#include "sampling/Distribution1D.h"
#include "sampling/SampleWarp.h"
#include "renderer/TraceableScene.h"
#include "cameras/Camera.h"
#include "media/Medium.h"
#include "math/TangentFrame.h"
#include "math/MathUtil.h"
#include "math/Angle.h"
#include "bsdfs/Bsdf.h"
#include <vector>
#include <memory>

struct TraceBase {
    const TraceableScene& scene;
    TraceSettings _settings;
    uint32 _threadId;

    // For computing direct lighting probabilities
    std::vector<float> _lightPdf;
    // For sampling light sources in adjoint light tracing
    std::unique_ptr<Distribution1D> _lightSampler;

    UniformPathSampler sampler {(uint32)readCycleCounter()};

    TraceBase(TraceableScene& scene, uint32 threadId);

    Vec3f trace(const vec3 O, const vec3 P, float& hitDistance, const int maxBounces = 16);
    Vec3f generalizedShadowRay(Ray& ray, const Medium* medium, const Primitive* endCap, int bounce);
};
