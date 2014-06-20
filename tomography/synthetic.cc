#include "random.h"
#include "file.h"
#include "view.h"
#include "layout.h"
#include "window.h"

VolumeF porousRock(int3 size) {
    assert(size.x == size.y);

    const float airDensity = 0.001; // Houndsfield ?
    VolumeF volume (size, airDensity);

    const float innerRadius = (1-4./100) * size.x/2.;
    const float outerRadius = (1-2./100) * size.x/2.;
    const float containerDensity = 5.6; // Pure iron
    for(uint y: range(size.y)) for(uint x: range(size.x)) {
        float r2 = sq(vec2(x,y)-vec2(size.xy()-1)/2.f);
        if(sq(innerRadius) < r2 && r2 < sq(outerRadius)) {
            for(uint z: range(size.z)) volume(x,y,z) = containerDensity;
        }
    }

    const float rate = 1./400; // 1/vx
    Random random; // Unseeded sequence (for repeatability)
    array<vec3> centers (2*rate*size.z*size.y*size.x); // 2 * Expected grain count (to avoid reallocation)
    for(uint z: range(size.z)) for(uint y: range(size.y)) for(uint x: range(size.x)) if(random()<rate) centers.insertAt(random%centers.size, vec3(x,y,z)); // Uniform distribution of grains, with randomly ordered integer centroid coordinates, without duplicates, may intersect
    // | generic buffer<T> shuffle(array<T>&& list) { buffer<T> shuffled(list.size) while(list) shuffled << list.take(random%list.size); } centers = shuffle(centers);

    const struct GrainType { const float probability; /*relative concentration*/ const float density; } types[] = {/*Rutile*/{0.7, 4.20}, /*Siderite*/{0.2, 3.96}, /*NaMontmorillonite*/{0.1, 2.65}};
    assert(sum(apply(grainCenters, [](GrainType t){return t.probability;})) == 1);
    const float radius  = 50; // vx
    uint begin = 0;
    for(GrainType type: types) { // Rasterize each grain type in order so that lighter grains overrides heavier grains
        uint end = begin + type.probability * centers.size;
        for(vec3 p : centers.slice(begin, end)) {
            float r2 = sq(p.xy()-vec2(size.xy()-1)/2.f);
            if(r2 >= sq(innerRadius-radius/2.)) continue; // Rejects grains intersecting container
            // Rasterizes grains as spheres
            for(int z: range(p.z-radius, p.z+radius+1)) {
                for(int y: range(p.y-radius, p.y+radius+1)) {
                    for(int x: range(p.x-radius, p.x+radius+1)) {
                        if(sq(vec3(x,y,z)-p) < sq(radius)) volume(x,y,z) = type.density; // TODO: Correct coverage (antialiasing)
                    }
                }
            }
        }
        begin = end;
    }
    return volume;
}

const uint N = fromInteger(arguments()[0]);
/*Phantom phantom (16);
CLVolume volume (N, phantom.volume(N));*/
CLVolume volume (porousRock(N));
SliceView sliceView (volume, 512/N);
VolumeView volumeView (volume, Projection(volume.size, volume.size), 512/N);
HBox layout ({ &sliceView , &volumeView });
Window window (&layout, str(N));
