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
        if(sq(innerRadius) < r2 && r2 < sq(outerRadius)) {
            for(uint z: range(size.z)) volume(x,y,z) = containerDensity;
        }
    }

    const float rate = 1./400; // 1/vx
    buffer<vec4> centers (2*rate*size.z*size.y*size.x, 0); // 2 * Expected grain count (to avoid reallocation)
    Random random; // Unseeded sequence (for repeatability)
    while(centers.size < centers.capacity) {
        const float maximumRadius = 8; // vx
        const float radius = random()*maximumRadius; // Uniform distribution of radius between [0, maximumRadius[
        const vec2 p = vec2(random(), random()) * vec2(size.xy());
        if(sq(p-vec2(volumeRadius)) > sq(innerRadius-radius)) continue;
        centers << vec4(p, radius+random()*(size.z-2*radius), radius);
    }

    {Time time;
    const struct GrainType { const float probability; /*relative concentration*/ const float density; } types[] = {/*Rutile*/{0.7, 4.20}, /*Siderite*/{0.2, 3.96}, /*NaMontmorillonite*/{0.1, 2.65}};
    assert(sum(apply(ref<GrainType>(types), [](GrainType t){return t.probability;})) == 1);
    uint begin = 0;
    for(GrainType type: types) { // Rasterize each grain type in order so that lighter grains overrides heavier grains
        uint end = begin + type.probability * centers.size;
        assert_(end <= centers.size);
        for(vec4 p : centers(begin, end)) {
            assert_(p.xyz()-vec3(p.w) >= vec3(0) && p.xyz()+vec3(p.w) < vec3(size), p);
            // Rasterizes grains as spheres
            for(int z: range(max(0, int(floor(p.z-p.w))), min(size.z, int(ceil(p.z+p.w))))) { // Grains may be cut by cylinder caps
                for(int y: range(floor(p.y-p.w), ceil(p.y+p.w))) {
                    for(int x: range(floor(p.x-p.w), ceil(p.x+p.w))) {
                        if(sq(vec3(x,y,z)-p.xyz()) < sq(p.w)) volume(x,y,z) = type.density; // TODO: Correct coverage (antialiasing)
                    }
                }
            }
        }
        begin = end;
    }
    log("Rasterization",time);}
    return volume;
}

const uint N = fromInteger(arguments()[0]);
CLVolume volume (porousRock(N));
SliceView sliceView (volume, 512/N);
VolumeView volumeView (volume, Projection(volume.size, volume.size), 512/N);
HBox layout ({ &sliceView , &volumeView });
Window window (&layout, str(N));
