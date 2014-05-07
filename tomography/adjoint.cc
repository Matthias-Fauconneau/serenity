#include "adjoint.h"
#include "update.h"

/// Computes residual r = p = At ( b - A x )
void Adjoint::initialize(const ref<Projection>& projections, const ref<ImageF>& images) {
    if(filter) for(Filter& filter: filters) filter = 2*images.first().width;
    const CylinderVolume volume (x);
    for(uint projectionIndex: range(projections.size)) { // TODO: Cluster rays for coherence (even from different projections)
        const Projection& projection = projections[projectionIndex];
        const ImageF& image = images[projectionIndex];
        parallel(image.height, [&image, &projection, &volume, this](uint id, uint y) {
            ref<float> row = image.data.slice(y*image.width, image.width);
            if(filter) row = filters[id].filter(row);
            for(uint x: range(image.width)) {
                v4sf start, end;
                if(intersect(projection, vec2(x, y), volume, start, end)) {
                    backproject(start, projection.ray, end, volume, AtAp[id], float8(/*scale **/ row[x])); // - Ax but x = 0
                }
            }
        });
    }
    // Merges r=p=sum(p#) and computes |r|
    float* pData = p;
    float* rData = r;
    float residualSum[coreCount] = {};
    chunk_parallel(p.size(), [pData, rData, &residualSum, this](uint id, uint offset, uint size) {
        float accumulator = 0;
        float* P[coreCount]; for(uint id: range(coreCount)) P[id] = AtAp[id];
        for(uint i: range(offset,offset+size)) {
            float Atb = 0;
            for(uint id: range(coreCount)) { Atb += P[id][i]; P[id][i]=0; } // Merges p and clears AtAp#
            // No regularization (x=0)
            pData[i] = Atb;
            rData[i] = Atb;
            accumulator += sq(Atb);
        }
        residualSum[id] += accumulator;
    });
    residualEnergy = sum(residualSum);
}

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + α p[k]
bool Adjoint::step(const ref<Projection>& projections, const ref<ImageF>& images) {
    totalTime.start();
    Time time; time.start();

    // Computes At A p (i.e projects and backprojects p)
    const CylinderVolume volume (p);
    for(uint projectionIndex: range(projections.size)) {
        const Projection& projection = projections[projectionIndex];
        const ImageF& image = images[projectionIndex];
        parallel(image.height, [&image, &projection, &volume, this](uint id, uint y) {
            if(filter) {
                mref<float> input = filters[id];
                v4sf start[input.size], end[input.size];
                uint first;
                for(first=0;first<input.size;first++) { if(intersect(projection, vec2(first, y), volume, start[first], end[first])) break; else input[first] = 0; }
                uint last = first;
                if(last<input.size) for(;;) {
                    input[last] = project(start[last], projection.ray, end[last], volume, p.data);
                    last++;
                    if(!(last<input.size)) break;
                    if(!intersect(projection, vec2(last, y), volume, start[last], end[last])) break;
                }
                for(uint x: range(last, input.size)) input[x] = 0;
                ref<float> output = filters[id].filter();
                for(uint x: range(first, last)) backproject(start[x], projection.ray, end[x], volume, AtAp[id], float8(output[x]));
            } else {
                for(uint x: range(image.width)) {
                    v4sf start, end;
                    if(intersect(projection, vec2(x, y), volume, start, end)) {
                        float Ax = project(start, projection.ray, end, volume, p.data);
                        backproject(start, projection.ray, end, volume, AtAp[id], float8(Ax));
                    }
                }
            }
        });
    }

    // Merges and clears AtAp and computes |p·Atp|
    float* AtApData = AtAp[0]; // Merges into first volume
    float* pData = p;
    float pAtApSum[coreCount] = {};
    parallel(1, p.sampleCount.z-1, [&projections, pData, AtApData, &pAtApSum, this](uint id, uint z) {
        float accumulator = 0;
        uint iz = z * p.sampleCount.y * p.sampleCount.x;
        for(uint y: range(1, p.sampleCount.y-1)) {
            uint izy = iz + y * p.sampleCount.x;
            for(uint x: range(1, p.sampleCount.x-1)) {
                uint i = izy + x;
                float* P[coreCount]; for(uint id: range(coreCount)) P[id] = AtAp[id];
                float AtAp = regularize ? regularization(p, x,y,z, i) : 0;
                for(uint id: range(coreCount)) { AtAp += P[id][i]; P[id][i]=0; } // Merges and clears AtAp#
                AtApData[i] = AtAp;
                accumulator += pData[i] * AtAp;
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