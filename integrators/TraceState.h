#include "media/Medium.h"
#include "math/Ray.h"

struct PathSampleGenerator;

// TODO: Modify TraceBase to primarily take/return TraceState as parameter
struct TraceState
{
    PathSampleGenerator &sampler;
    Medium::MediumState mediumState;
    const Medium *medium;
    bool wasSpecular;
    Ray ray;
    int bounce;

    TraceState(PathSampleGenerator &sampler_)
    : sampler(sampler_),
      medium(nullptr),
      wasSpecular(true),
      ray(Vec3f(0.0f), Vec3f(0.0f)),
      bounce(0)
    {
        mediumState.reset();
    }
};
