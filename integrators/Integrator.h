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

    uint32 _currentSpp;
    uint32 _nextSpp;

    void advanceSpp();

    void writeBuffers(const std::string &suffix, bool overwrite);

    virtual void saveState(OutputStreamHandle &out) = 0;
    virtual void loadState(InputStreamHandle &in) = 0;

public:
    Integrator();
    virtual ~Integrator();

    virtual void prepareForRender(TraceableScene &scene, uint32 seed) = 0;
    virtual void teardownAfterRender() = 0;

    virtual void startRender(std::function<void()> completionCallback) = 0;
    virtual void waitForCompletion() = 0;
    virtual void abortRender() = 0;

    void saveOutputs();
    void saveCheckpoint();

    void saveRenderResumeData(Scene &scene);
    bool resumeRender(Scene &scene);

    bool done() const
    {
        return _currentSpp >= _nextSpp;
    }

    uint32 currentSpp() const
    {
        return _currentSpp;
    }

    uint32 nextSpp() const
    {
        return _nextSpp;
    }
};
