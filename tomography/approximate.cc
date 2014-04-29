#include "approximate.h"
#include "update.h"

/// Computes residual r = p = At ( b - A x )
void Approximate::initialize(const ref<Projection>& projections, const ref<ImageF>& images) {
    if(filter) for(Filter& filter: filters) filter = 2*images.first().width;
    // Filters source images
    if(filter) for(const ImageF& image: images) {
        parallel(image.height, [&image, this](uint id, uint y) {
            mref<float> imageRow = image.data.slice(y*image.width, image.width);
            ref<float> filteredRow = filters[id].filter(imageRow);
            for(uint x: range(image.width))  imageRow[x] = filteredRow[x];
        });
    }
    float* pData = p;
    float* rData = r;
    float residualSum[coreCount] = {};
    // Approximates trilinear backprojection with bilinear sample (reverse splat)
    const vec3 center = vec3(p.sampleCount-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    const vec2 imageCenter = vec2(images.first().size()-int2(1))/2.f;
    parallel(1, p.sampleCount.z-1, [&projections, &images, center, radiusSq, imageCenter, pData, rData, &residualSum, this](uint id, uint z) {
        float accumulator = 0;
        uint iz = z * p.sampleCount.y * p.sampleCount.x;
        for(uint y: range(1, p.sampleCount.y-1)) {
            uint izy = iz + y * p.sampleCount.x;
            for(uint x: range(1, p.sampleCount.x-1)) {
                const vec3 origin = vec3(x,y,z) - center;
                if(sq(origin.xy()) < radiusSq) {
                    float Atb = 0;
                    for(uint projectionIndex: range(projections.size)) {
                        const Projection& projection = projections[projectionIndex];
                        const ImageF& P = images[projectionIndex];
                        vec2 xy = (projection.projection * origin).xy() + imageCenter;
                        uint i = xy.x, j = xy.y;
                        float u = fract(xy.x), v = fract(xy.y);
                        assert(i<P.width && j<P.height, i,j, u,v);
                        float s = (1-v) * ((1-u) * P(i,j  ) + u * P(i+1,j  )) +
                                v  * ((1-u) * P(i,j+1) + u * P(i+1,j+1)) ;
                        Atb += s;
                    }
                    pData[izy+x] = Atb;
                    rData[izy+x] = Atb;
                    accumulator += sq(Atb);
                }
            }
        }
        residualSum[id] += accumulator;
    });
    residualEnergy = sum(residualSum);
}

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + α p[k]
bool Approximate::step(const ref<Projection>& projections, const ref<ImageF>& images) {
    totalTime.start();
    Time time; time.start();

    // Projects p
    const CylinderVolume volume (p);
    for(uint projectionIndex: range(projections.size)) {
        const Projection& projection = projections[projectionIndex];
        const ImageF& image = images[projectionIndex];
        parallel(image.height, [&image, &projection, &volume, this](uint id, uint y) {
            mref<float> output = image.data.slice(y*image.width, image.width);
            mref<float> input = filter ? filters[id] : output;
            for(uint x: range(image.width)) {
                v4sf start, end;
                input[x] = intersect(projection, vec2(x, y), volume, start, end) ? project(start, projection.ray, end, volume, p) : 0;
            }
            if(filter) {
                ref<float> filtered = filters[id].filter(input);
                for(uint x: range(image.width)) output[x] = filtered[x];
            }
        });
    }

    // Merges and clears AtAp and computes |p·Atp|
    float* AtApData = AtAp;
    float* pData = p;
    float pAtApSum[coreCount] = {};
    // Approximates trilinear backprojection with bilinear sample (reverse splat)
    const vec3 center = vec3(p.sampleCount-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    const vec2 imageCenter = vec2(images.first().size()-int2(1))/2.f;
    parallel(1, p.sampleCount.z-1, [&projections, &images, center, radiusSq, imageCenter, pData, AtApData, &pAtApSum, this](uint id, uint z) {
        float accumulator = 0;
        uint iz = z * p.sampleCount.y * p.sampleCount.x;
        for(uint y: range(1, p.sampleCount.y-1)) {
            uint izy = iz + y * p.sampleCount.x;
            for(uint x: range(1, p.sampleCount.x-1)) {
                const vec3 origin = vec3(x,y,z) - center;
                if(sq(origin.xy()) < radiusSq) {
                    float AtAp = 0;
                    for(uint projectionIndex: range(projections.size)) {
                        const Projection& projection = projections[projectionIndex];
                        const ImageF& P = images[projectionIndex];
                        vec2 xy = (projection.projection * origin).xy() + imageCenter;
                        uint i = xy.x, j = xy.y;
                        float u = fract(xy.x), v = fract(xy.y);
                        assert(i<P.width && j<P.height, i,j, u,v);
                        float s = (1-v) * ((1-u) * P(i,j  ) + u * P(i+1,j  )) +
                                v  * ((1-u) * P(i,j+1) + u * P(i+1,j+1)) ;
                        AtAp += s;
                    }
                    AtApData[izy+x] = AtAp;
                    accumulator += pData[izy+x] * AtAp;
                }
            }
        }
        pAtApSum[id] += accumulator;
    });
    float pAtAp = sum(pAtApSum);

    // Updates x += α p, r -= α AtAp, clears AtAp, computes |r|²
    float alpha = residualEnergy / pAtAp;
    float* xData = x;
    float* rData = r;
    float newResidualSum[coreCount] = {};
    chunk_parallel(p.size(), [alpha, pData, xData, AtApData, rData, &newResidualSum](uint id, uint offset, uint size) {
        float accumulator = 0;
        for(uint i: range(offset,offset+size)) {
            xData[i] += alpha * pData[i];
            rData[i] -= alpha * AtApData[i];
            AtApData[i] = 0;
            accumulator += sq(rData[i]);
        }
        newResidualSum[id] += accumulator;
    });
    float newResidual = sum(newResidualSum);
    float beta = newResidual / residualEnergy;

    // Computes next search direction: p[k+1] = r[k+1] + β p[k]
    chunk_parallel(p.size(), [&](uint, uint offset, uint size) { for(uint i: range(offset,offset+size)) pData[i] = rData[i] + beta * pData[i]; });

    time.stop();
    totalTime.stop();
    k++;
    log_(str(dec(k,2), time, str(totalTime.toFloat()/k)+"s"_));
    residualEnergy = newResidual;
    return true;
}
