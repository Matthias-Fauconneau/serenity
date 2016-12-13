#include "TraceSettings.h"
#include "samplerecords/SurfaceScatterEvent.h"
#include "samplerecords/MediumSample.h"
#include "samplerecords/LightSample.h"
#include "sampling/PathSampleGenerator.h"
#include "sampling/UniformSampler.h"
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
#include <cmath>

struct TraceBase {
    const TraceableScene *_scene;
    TraceSettings _settings;
    uint32 _threadId;

    // For computing direct lighting probabilities
    std::vector<float> _lightPdf;
    // For sampling light sources in adjoint light tracing
    std::unique_ptr<Distribution1D> _lightSampler;

    TraceBase(TraceableScene *scene, const TraceSettings &settings, uint32 threadId);

    bool isConsistent(const SurfaceScatterEvent &event, const Vec3f &w) const;

    template<bool ComputePdfs>
    inline Vec3f generalizedShadowRayImpl(PathSampleGenerator &sampler,
                               Ray &ray,
                               const Medium *medium,
                               const Primitive *endCap,
                               int bounce,
                               bool startsOnSurface,
                               bool endsOnSurface,
                               float &pdfForward,
                               float &pdfBackward) const;

    Vec3f attenuatedEmission(PathSampleGenerator &sampler,
                             const Primitive &light,
                             const Medium *medium,
                             float expectedDist,
                             IntersectionTemporary &data,
                             IntersectionInfo &info,
                             int bounce,
                             Ray &ray,
                             Vec3f *transmittance);

    bool volumeLensSample(const Camera &camera,
                    PathSampleGenerator &sampler,
                    MediumSample &mediumSample,
                    const Medium *medium,
                    int bounce,
                    const Ray &parentRay,
                    Vec3f &weight,
                    Vec2f &pixel);

    bool surfaceLensSample(const Camera &camera,
                    SurfaceScatterEvent &event,
                    const Medium *medium,
                    int bounce,
                    const Ray &parentRay,
                    Vec3f &weight,
                    Vec2f &pixel);

    Vec3f lightSample(const Primitive &light,
                      SurfaceScatterEvent &event,
                      const Medium *medium,
                      int bounce,
                      const Ray &parentRay,
                      Vec3f *transmittance);

    Vec3f bsdfSample(const Primitive &light,
                     SurfaceScatterEvent &event,
                     const Medium *medium,
                     int bounce,
                     const Ray &parentRay);

    Vec3f volumeLightSample(PathSampleGenerator &sampler,
                        MediumSample &mediumSample,
                        const Primitive &light,
                        const Medium *medium,
                        int bounce,
                        const Ray &parentRay);

    Vec3f volumePhaseSample(const Primitive &light,
                        PathSampleGenerator &sampler,
                        MediumSample &mediumSample,
                        const Medium *medium,
                        int bounce,
                        const Ray &parentRay);

    Vec3f sampleDirect(const Primitive &light,
                       SurfaceScatterEvent &event,
                       const Medium *medium,
                       int bounce,
                       const Ray &parentRay,
                       Vec3f *transmittance);

    Vec3f volumeSampleDirect(const Primitive &light,
                        PathSampleGenerator &sampler,
                        MediumSample &mediumSample,
                        const Medium *medium,
                        int bounce,
                        const Ray &parentRay);

    const Primitive *chooseLight(PathSampleGenerator &sampler, const Vec3f &p, float &weight);
    const Primitive *chooseLightAdjoint(PathSampleGenerator &sampler, float &pdf);

    Vec3f volumeEstimateDirect(PathSampleGenerator &sampler,
                        MediumSample &mediumSample,
                        const Medium *medium,
                        int bounce,
                        const Ray &parentRay);

    Vec3f estimateDirect(SurfaceScatterEvent &event,
                         const Medium *medium,
                         int bounce,
                         const Ray &parentRay,
                         Vec3f *transmission);

public:
    SurfaceScatterEvent makeLocalScatterEvent(IntersectionTemporary &data, IntersectionInfo &info,
            Ray &ray, PathSampleGenerator *sampler) const;

    Vec3f generalizedShadowRay(PathSampleGenerator &sampler,
                               Ray &ray,
                               const Medium *medium,
                               const Primitive *endCap,
                               int bounce) const;
    Vec3f generalizedShadowRayAndPdfs(PathSampleGenerator &sampler,
                               Ray &ray,
                               const Medium *medium,
                               const Primitive *endCap,
                               int bounce,
                               bool startsOnSurface,
                               bool endsOnSurface,
                               float &pdfForward,
                               float &pdfBackward) const;

    bool handleVolume(PathSampleGenerator &sampler, MediumSample &mediumSample,
               const Medium *&medium, int bounce, bool adjoint, bool enableLightSampling,
               Ray &ray, Vec3f &throughput, Vec3f &emission, bool &wasSpecular);

    bool handleSurface(SurfaceScatterEvent &event, IntersectionTemporary &data,
               IntersectionInfo &info, const Medium *&medium,
               int bounce, bool adjoint, bool enableLightSampling, Ray &ray,
               Vec3f &throughput, Vec3f &emission, bool &wasSpecular,
               Medium::MediumState &state, Vec3f *transmittance = nullptr);

    void handleInfiniteLights(IntersectionTemporary &data,
            IntersectionInfo &info, bool enableLightSampling, Ray &ray,
            Vec3f throughput, bool wasSpecular, Vec3f &emission);
};
