#include "MLEM.h"
#include "update.h"

void MLEM::initialize(const ref<Projection>&, const ref<ImageF>& images) {
    float sum = 0; for(const ImageF& image: images) sum += ::sum(image.data);
    //float volume = PI*((x.sampleCount.x-1)/2)*((x.sampleCount.y-1)/2)*x.sampleCount.z;
    float volume = 0;
    const uint X=x.sampleCount.x, Y=x.sampleCount.y, Z=x.sampleCount.z;
    const vec3 center = vec3(x.sampleCount-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    for(uint z: range(Z)) for(uint y: range(Y)) for(uint x: range(X)) { const vec3 origin = vec3(x,y,z) - center; if(sq(origin.xy()) < radiusSq) volume++; }
    float mean = sum / images.size / volume; // Projection energy / volume of the support
    for(uint z: range(Z)) for(uint y: range(Y)) for(uint x: range(X)) { const vec3 origin = vec3(x,y,z) - center; if(sq(origin.xy()) < radiusSq) this->x[z*X*Y + y*X + x] = mean; }
}

/// Optimizes Poisson parameter vector Ax to maximize likelihood p(b|x) using Newton's method: x[k+1] = x[k] - f(x[k])/f'(x[k])
bool MLEM::step(const ref<Projection>& projections, const ref<ImageF>& images) {
    totalTime.start();
    Time time; time.start();

    const CylinderVolume volume (x);
    float* xData = x;
    const uint X=x.sampleCount.x, Y=x.sampleCount.y, Z=x.sampleCount.z;
    const vec3 center = vec3(x.sampleCount-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    const vec2 imageCenter = vec2(images.first().size()-int2(1))/2.f;
    chunk_parallel(X*Y*Z, [this, &projections, &images, xData, X, Y, Z,  center, radiusSq, imageCenter, &volume](uint id, uint, uint size) {
        Random& random = this->random[id];
        for(uint unused i: range(size)) {
            uint x = random%X, y = random%Y, z = random%Z;
            const vec3 origin = vec3(x,y,z) - center;
            if(sq(origin.xy()) < radiusSq) {
                float sum = 0; float count = 0;
                uint projectionIndex = random%projections.size; // SMART = multiplicative SART = simultaneous MART)
                //for(uint projectionIndex: range(projections.size)) {
                    const Projection& projection = projections[projectionIndex];
                    const ImageF& P = images[projectionIndex];
                    vec2 xy = (projection.projection * origin).xy() + imageCenter;
                    uint i = xy.x, j = xy.y;
                    float u = fract(xy.x), v = fract(xy.y);
                    assert(i<P.width && j<P.height, i,j, u,v);
                    float b = (1-v) * ((1-u) * P(i,j  ) + u * P(i+1,j  )) +
                                       v  * ((1-u) * P(i,j+1) + u * P(i+1,j+1)) ;
                    v4sf start,end;
                    intersect(projection, xy, volume, start, end);
                    float Ax = project(start, projection.ray, end, volume, xData);
                    if(Ax) { sum += b / Ax; count += 1; } else assert(!b);
                //}
                float& xv = xData[z*X*Y + y*X + x];
                xv = min(xv * max(float(projections.size-1)/projections.size, sum / count), 1.f);
            } else assert(xData[z*X*Y + y*X + x] == 0);
        }
    });
    time.stop();
    totalTime.stop();
    k++;
    //log_(str(dec(k,2), time, str(totalTime.toFloat()/k)+"s"_));
    return true;
}

