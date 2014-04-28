#include "adjoint.h"
#include "update.h"

/// Computes residual r = p = At ( b - A x )
void Adjoint::initialize(const ref<Projection>& projections, const ref<ImageF>& images) {
    const CylinderVolume volume (x);
    for(uint projectionIndex: range(projections.size)) { // TODO: Cluster rays for coherence (even from different projections)
        const Projection& projection = projections[projectionIndex];
        const ImageF& image = images[projectionIndex];
        float* const imageData = image.data;
        uint imageWidth = image.width;
        parallel(image.data.size, [&projection, &volume, imageData, imageWidth, this](uint id, uint index) { int x=index%imageWidth, y=index/imageWidth; update(projection, vec2(x, y), volume, imageData[y*imageWidth+x], AtAp[id]); });
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
            for(uint id: range(coreCount)) { Atb -= P[id][i]; /*- as we compute Ax - b instead of b - Ax (to factorize update for step)*/ P[id][i]=0; } // Merges p and clears AtAp#
            //Atb -= regularization(x, index); // = 0
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
        uint imageWidth = image.width;
        parallel(image.data.size, [this, &volume, &projection, imageWidth](uint id, uint index) { int x=index%imageWidth, y=index/imageWidth; update(projection, vec2(x, y), volume, 0, AtAp[id]); });
    }

    // Merges and clears AtAp and computes |p·Atp|
    float* AtApData = AtAp[0]; // Merges into first volume
    float* pData = p;
    float pAtApSum[coreCount] = {};
    chunk_parallel(p.size(), [pData, AtApData, &pAtApSum, this](uint id, uint offset, uint size) {
        float accumulator = 0;
        float* P[coreCount]; for(uint id: range(coreCount)) P[id] = AtAp[id];
        for(uint i: range(offset,offset+size)) {
            float AtAp = 0;
            for(uint id: range(coreCount)) { AtAp += P[id][i]; P[id][i]=0; } // Merges and clears AtAp#
            AtApData[i] = AtAp;
            accumulator += pData[i] * AtAp;
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
