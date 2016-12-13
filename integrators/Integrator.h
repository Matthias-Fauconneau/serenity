#pragma once
#include "io/JsonSerializable.h"
#include "io/FileUtils.h"
#include <functional>

struct TraceableScene;
struct Scene;

struct Integrator : public JsonSerializable
{
protected:
    const TraceableScene *_scene;

public:
    Integrator();
    virtual ~Integrator();

    virtual void prepareForRender(TraceableScene &scene, uint32 seed) = 0;
    virtual void teardownAfterRender() = 0;
};
