#ifndef PATHTRACER_HPP_
#define PATHTRACER_HPP_

#include "PathTracerSettings.h"

#include "integrators/TraceBase.h"

namespace Tungsten {

class PathTracer : public TraceBase
{
    PathTracerSettings _settings;
    bool _trackOutputValues;

public:
    PathTracer(TraceableScene *scene, const PathTracerSettings &settings, uint32 threadId);

    Vec3f traceSample(Vec2u pixel, PathSampleGenerator &sampler);
};

}

#endif /* PATHTRACER_HPP_ */
