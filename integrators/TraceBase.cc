#include "TraceBase.h"

TraceBase::TraceBase(TraceableScene& scene, uint32 threadId) : scene(scene), _threadId(threadId) {
    _lightPdf.resize(scene.lights().size());

    std::vector<float> lightWeights(scene.lights().size());
    for (size_t i = 0; i < scene.lights().size(); ++i) {
        scene.lights()[i]->makeSamplable(scene, _threadId);
        lightWeights[i] = 1.0f; // TODO: Use light power here
    }
    _lightSampler.reset(new Distribution1D(std::move(lightWeights)));

    for (const auto &prim : scene.lights())
        prim->makeSamplable(scene, _threadId);
}

Vec3f TraceBase::trace(const vec3 O, const vec3 P, float& hitDistance) {
    PositionSample position;
    position.p.x() = O.x;
    position.p.y() = O.y;
    position.p.z() = O.z;
    position.weight = Vec3f(1.0f);
    position.pdf = 1.0f;
    position.Ng = scene._camera->_transform.fwd();
    DirectionSample direction;
    const vec3 d = normalize(P-O);
    direction.d.x() = d.x;
    direction.d.y() = d.y;
    direction.d.z() = d.z;
    direction.weight = Vec3f(1.0f);
    direction.pdf = 1;

    Vec3f throughput (1);
    Ray ray(position.p, direction.d);
    ray.setPrimaryRay(true);

    MediumSample mediumSample;
    Medium::MediumState state; state.reset();
    Vec3f emission(0.0f);
    const Medium* medium = 0;
    hitDistance = inff;
    bool wasSpecular = true;
    for(int bounce = 0;;bounce++) {
        IntersectionInfo info;
        IntersectionTemporary data;
        info.primitive = nullptr;
        data.primitive = nullptr;
        TraceableScene::IntersectionRay eRay(convert(ray), data, ray, scene._userGeomId);
        rtcIntersect(scene._scene, eRay);

        bool didHit;
        if (data.primitive) {
            info.p = ray.pos() + ray.dir()*ray.farT();
            info.w = ray.dir();
            info.epsilon = scene.DefaultEpsilon;
            data.primitive->intersectionInfo(data, info);
            didHit = true;
        } else {
            didHit = false;
        }
        constexpr int maxBounces = 16;
        if((!didHit && !medium) || bounce >= maxBounces) {
            if(scene.intersectInfinites(ray, data, info)) {
                if(wasSpecular || !info.primitive->isSamplable())
                    emission += throughput*info.primitive->evalDirect(data, info);
            }
            break;
        }

        bool hitSurface = true;
        if (medium) {
            if (!medium->sampleDistance(sampler, ray, state, mediumSample))
                break;
            throughput *= mediumSample.weight;
            hitSurface = mediumSample.exited;
            if(hitSurface && !didHit) {
                if(scene.intersectInfinites(ray, data, info)) {
                    if(wasSpecular || !info.primitive->isSamplable())
                        emission += throughput*info.primitive->evalDirect(data, info);
                }
                break;
            }
        }

        if (hitSurface) {
            if(bounce == 0) hitDistance = ray.farT();

            TangentFrame frame;
            info.primitive->setupTangentFrame(data, info, frame);
            SurfaceScatterEvent event(&info, &sampler, frame, frame.toLocal(-ray.dir()), BsdfLobes::AllLobes, false );
            Vec3f transmittance(-1.0f);

            const Bsdf &bsdf = *info.bsdf;
            Vec3f transparency = bsdf.eval(event.makeForwardEvent(), false);
            float transparencyScalar = transparency.avg();

            Vec3f wo;
            if (event.sampler->nextBoolean(transparencyScalar) ){
                wo = ray.dir();
                event.pdf = transparencyScalar;
                event.weight = transparency/transparencyScalar;
                event.sampledLobe = BsdfLobes::ForwardLobe;
                throughput *= event.weight;
            } else {
                if (bounce < maxBounces - 1 && !(event.info->bsdf->lobes().isPureSpecular() || event.info->bsdf->lobes().isForward())) {
                    assert(scene.lights().size() == 1);
                    const Primitive& light = *scene.lights()[0].get();
                    Vec3f result (0.0f);
                    LightSample sample;
                    if(light.sampleDirect(_threadId, event.info->p, *event.sampler, /*out*/ sample)) {
                        event.wo = event.frame.toLocal(sample.d);

                        bool geometricBackside = (sample.d.dot(event.info->Ng) < 0.0f);
                        medium = event.info->primitive->selectMedium(medium, geometricBackside);

                        event.requestedLobe = BsdfLobes::AllButSpecular;

                        Vec3f f = event.info->bsdf->eval(event, false);
                        if(f != 0.0f) {

                            Ray lightRay = ray.scatter(event.info->p, sample.d, event.info->epsilon);
                            lightRay.setPrimaryRay(false);

                            IntersectionTemporary data;
                            IntersectionInfo info;
                            const float expectedDist = sample.dist;
                            constexpr float fudgeFactor = 1.0f + 1e-3f;

                            if(light.intersect(lightRay, data) && lightRay.farT()*fudgeFactor >= expectedDist) {
                                info.p = lightRay.pos() + lightRay.dir()*lightRay.farT();
                                info.w = lightRay.dir();
                                light.intersectionInfo(data, info);
                                Vec3f shadow = generalizedShadowRay(lightRay, medium, &light, bounce);
                                transmittance = shadow;
                                if(shadow != 0.0f) {
                                    Vec3f e = shadow*light.evalDirect(data, info);
                                    if (e != 0.0f) {
                                        result += f*e/sample.pdf*powerHeuristic(sample.pdf, event.info->bsdf->pdf(event));
                                    }
                                }
                            }
                        }
                    }
                    event.requestedLobe = BsdfLobes::AllButSpecular;
                    if(event.info->bsdf->sample(event, false) && event.weight != 0.0f) {
                        Vec3f wo = event.frame.toGlobal(event.wo);

                        bool geometricBackside = (wo.dot(event.info->Ng) < 0.0f);
                        medium = event.info->primitive->selectMedium(medium, geometricBackside);

                        Ray bsdfRay = ray.scatter(event.info->p, wo, event.info->epsilon);
                        bsdfRay.setPrimaryRay(false);

                        IntersectionTemporary data;
                        IntersectionInfo info;
                        const float expectedDist = sample.dist;
                        constexpr float fudgeFactor = 1.0f + 1e-3f;

                        if(light.intersect(bsdfRay, data) && bsdfRay.farT()*fudgeFactor >= expectedDist) {
                            info.p = bsdfRay.pos() + bsdfRay.dir()*bsdfRay.farT();
                            info.w = bsdfRay.dir();
                            light.intersectionInfo(data, info);
                            Vec3f shadow = generalizedShadowRay(bsdfRay, medium, &light, bounce);
                            transmittance = shadow;
                            if(shadow != 0.0f) {
                                Vec3f e = shadow*light.evalDirect(data, info);
                                if (e != 0.0f) {
                                    result += e*event.weight*powerHeuristic(event.pdf, light.directPdf(_threadId, data, info, event.info->p));
                                }
                            }
                        }
                    }
                    emission += result*throughput;
                }
                if (info.primitive->isEmissive()) {
                    if (wasSpecular || !info.primitive->isSamplable())
                        emission += info.primitive->evalDirect(data, info)*throughput;
                }

                event.requestedLobe = BsdfLobes::AllLobes;
                if (!bsdf.sample(event, false)) break;

                wo = event.frame.toGlobal(event.wo);

                throughput *= event.weight;
                wasSpecular = event.sampledLobe.hasSpecular();
                if (!wasSpecular)
                    ray.setPrimaryRay(false);
            }

            bool geometricBackside = (wo.dot(info.Ng) < 0.0f);
            medium = info.primitive->selectMedium(medium, geometricBackside);
            state.reset();

            ray = ray.scatter(ray.hitpoint(), wo, info.epsilon);
        }

        if (throughput.max() == 0.0f) {
            if(scene.intersectInfinites(ray, data, info)) {
                if(wasSpecular || !info.primitive->isSamplable())
                    emission += throughput*info.primitive->evalDirect(data, info);
            }
            break;
        }

        float roulettePdf = std::abs(throughput).max();
        if (bounce > 2 && roulettePdf < 0.1f) {
            if (sampler.nextBoolean(roulettePdf))
                throughput /= roulettePdf;
            else
                break;
        }
    }
    return emission;
}

inline Vec3f TraceBase::generalizedShadowRay(Ray &ray, const Medium *medium, const Primitive *endCap, int bounce) {
    IntersectionTemporary data;
    IntersectionInfo info;

    float initialFarT = ray.farT();
    Vec3f throughput(1.0f);
    for(;;) {
        bool didHit = scene.intersect(ray, data, info) && info.primitive != endCap;
        if (didHit) {
            if (!info.bsdf->lobes().hasForward())
                return Vec3f(0.0f);

            TangentFrame frame;
            info.primitive->setupTangentFrame(data, info, frame);
            SurfaceScatterEvent event(&info, &sampler, frame, frame.toLocal(-ray.dir()), BsdfLobes::AllLobes, false);

            // For forward events, the transport direction does not matter (since wi = -wo)
            Vec3f transparency = info.bsdf->eval(event.makeForwardEvent(), false);
            if (transparency == 0.0f)
                return Vec3f(0.0f);

            throughput *= transparency;
            bounce++;

            if (bounce >= _settings.maxBounces)
                return Vec3f(0.0f);
        }

        if (medium) throughput *= medium->transmittance(sampler, ray);
        if (info.primitive == nullptr || info.primitive == endCap)
            return bounce >= _settings.minBounces ? throughput : Vec3f(0.0f);
        medium = info.primitive->selectMedium(medium, !info.primitive->hitBackside(data));

        ray.setPos(ray.hitpoint());
        initialFarT -= ray.farT();
        ray.setNearT(info.epsilon);
        ray.setFarT(initialFarT);
    }
    return Vec3f(0.0f);
}
