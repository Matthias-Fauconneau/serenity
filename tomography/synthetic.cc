#include "random.h"
#include "file.h"
#include "view.h"
#include "layout.h"
#include "window.h"

VolumeF porousRock(int3 size) {
    assert(size.x == size.y);

    const float airDensity = 0.001; // Houndsfield ?
    VolumeF volume (size, airDensity);

    const vec2 center = vec2(size.xy()-int2(1))/2.f;
    const float volumeRadius = center.x;
    const float innerRadius = (1-4./100) * volumeRadius;
    const float outerRadius = (1-2./100) * volumeRadius;
    const float containerDensity = 5.6; // Pure iron
    for(uint y: range(size.y)) for(uint x: range(size.x)) {
        float r2 = sq(vec2(x,y)-vec2(size.xy()-1)/2.f);
        if(sq(innerRadius-1./2) < r2 && r2 < sq(outerRadius+1./2)) {
            for(uint z: range(size.z)) {
                if(sq(innerRadius+1./2) <= r2 && r2 <= sq(outerRadius-1./2)) volume(x,y,z) = containerDensity;
                else if(r2 < sq(innerRadius+1./2)) {
                    float r = sqrt(r2);
                    float c = r - (innerRadius-1./2);
                    volume(x,y,z) = c * containerDensity;
                } else if(sq(outerRadius-1./2) < r2) {
                    float r = sqrt(r2);
                    float c = (outerRadius+1./2) - r;
                    volume(x,y,z) = c * containerDensity;
                } else error(r2);
            }
        }
    }

    const struct GrainType { const float probability; /*relative concentration*/ const float density; } types[] = {/*Rutile*/{0.7, 4.20}, /*Siderite*/{0.2, 3.96}, /*NaMontmorillonite*/{0.1, 2.65}};
    assert(sum(apply(ref<GrainType>(types), [](GrainType t){return t.probability;})) == 1);
    const float rate = 1./400; // 1/vx
    const uint grainCount = rate*size.z*size.y*size.x;
    Random random; // Unseeded sequence (for repeatability)
    Time time;
    for(GrainType type: types) { // Rasterize each grain type in order so that lighter grains overrides heavier grains
        float* volumeData = volume.data;
        const uint XY = size.x*size.x;
        const uint X = size.x;
        for(uint count=0; count < type.probability * grainCount;) {
            const float maximumRadius = 8; // vx
            const float r = random()*maximumRadius; // Uniform distribution of radius between [0, maximumRadius[
            const vec2 xy = vec2(random(), random()) * vec2(size.xy());
            if(sq(xy-vec2(volumeRadius)) > sq(innerRadius-r)) continue;
            count++;
            float cx = xy.x, cy=xy.y, cz=r+random()*((size.z-1)-2*r);
            float innerR2 = sq(r-1.f/2);
            float outerRadius = r+1.f/2;
            float outerR2 = sq(outerRadius);
            float v = type.density;
            // Rasterizes grains as spheres
            int iz = cz-r, iy = cy-r, ix = cx-r;
            float fz = cz-iz, fy=cy-iy, fx=cx-ix;
            uint grainSize = ceil(2*r);
            float* volume0 = volumeData + iz*XY + iy*X + ix;
            for(uint z=0; z<grainSize; z++) { // Grains may be cut by cylinder caps
                float* volumeZ = volume0 + z*XY;
                float rz = float(z) - fz;
                float RZ = rz*rz;
                for(uint y=0; y<grainSize; y++) {
                    float* volumeZY = volumeZ + y*X;
                    float ry = float(y) - fy;
                    float RZY = RZ + ry*ry;
                    for(uint x=0; x<grainSize; x++) {
                        float rx = float(x) - fx;
                        float r2 = RZY + rx*rx;
                        if(r2 < outerR2) {
                            if(r2 <= innerR2) volumeZY[x] = v;
                            else {
                                float c = outerRadius - sqrt(r2);
                                volumeZY[x] = (1-c) * volumeZY[x] + c * v; // Alpha blending
                            }
                        }
                    }
                }
            }
        }
    }
    log(time);
    return volume;
}

struct App {
    const int N = fromInteger(arguments()[0]);
    VolumeF hostVolume = normalize(porousRock(N));
    CLVolume volume {hostVolume};
    SliceView sliceView {volume, 512/N};
    Projection A {volume.size, volume.size};
    VolumeView volumeView {volume, A, 512/N};
    HBox layout {{ &sliceView , &volumeView }};
    Window window {&layout, str(N)};
    App() {
        writeFile("Data/"_+strx(hostVolume.size)+".ref"_, cast<byte>(hostVolume.data));
    }
} app;
