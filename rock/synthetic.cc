#include "volume-operation.h"
#include "time.h"

class(Synthetic, Operation), virtual VolumeOperation {
    const int3 size = 128;

    uint outputSampleSize(uint index) override { if(index) return 0; /*Extra outputs*/ return sizeof(uint8); }
    size_t outputSize(const Dict&, const ref<const Result*>&, uint index) override {
        if(index) return 0; //Extra outputs
        return (uint64)size.x*size.y*size.z;
    }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>&, const ref<Result*>& otherOutputs) override {
        Volume8& target = outputs.first();
        assert_(target.data.data, target.data.data, target.data.size, target.data.capacity);
        target.sampleCount = size;
        target.field = String("μ"_);
        target.maximum = 0xFF;
        clear<uint8>(target, target.size(), uint8(0xFF));
        const uint maximumRadius = (size.x-1)/4;
        struct Sphere { vec3 center; vec3 radius; };
        array<Sphere> spheres;
        struct Tube { vec3 A, B; float radius; };
        array<Tube> tubes;
        for(Random random; spheres.size < (uint)size.x; ) {
            vec3 radius = vec3(1)+vec3(random(),random(),random())*float(maximumRadius-1);
            if(max(max(radius.x,radius.y),radius.z) > 4*min(min(radius.x,radius.y),radius.z)) continue; // Limits aspect ratio
            const vec3 margin = radius+vec3(1);
            vec3 center = margin+vec3(random(),random(),random())*(vec3(size) - 2.f*margin);
            Sphere nearest = {vec3(0),0}; float distance=size.x;
            for(const Sphere& o: spheres) {
                if(abs(o.center-center) <= o.radius+radius) goto break_; // Prevents intersections (FIXME: box)
                if(norm(o.center-center)<distance) distance=norm(o.center-center), nearest = o;
            }
            /*else*/ {
                { // Rasterizes spheres (pores)
                    int3 min =clip(int3(0),int3(floor(center))- int3(ceil(radius)),target.sampleCount);
                    int3 max=clip(int3(0),int3(ceil  (center))+int3(ceil(radius)),target.sampleCount);
                    for(int z: range(min.z, max.z)) for(int y: range(min.y, max.y)) for(int x: range(min.x, max.x)) {
                        if(sq((vec3(x,y,z)-center)/radius) <= 1) target(x,y,z) = 0;
                    }
                    spheres << Sphere{center, radius};
                }
                /*if(nearest.radius) { // Rasterizes tubes (throats)
                    vec3 A = center, B = nearest.center;
                    float tubeRadius = min(norm(radius), norm(nearest.radius))/2;
                    int3 min=clip(int3(0),int3(::min(A,B))-int3(radius),target.sampleCount);
                    int3 max=clip(int3(0),int3(ceil(::max(A,B)))+int3(radius),target.sampleCount);
                    for(int z: range(min.z, max.z)) for(int y: range(min.y, max.y)) for(int x: range(min.x, max.x)) {
                        vec3 P = vec3(x,y,z);
                        float t = dot(B-A, P-A)/sq(B-A);
                        if(t<=0 || t>=1) continue;
                        //if(sq(P - (A+t*(B-A))) < sq((1-2*t*(1-t))*tubeRadius)) target(x,y,z) = 0; // Tapered cylinder
                        if(sq(P - (A+t*(B-A))) < sq(tubeRadius)) target(x,y,z) = 0; // Straight cylinder
                    }
                    tubes << Tube{A, B, tubeRadius};
                }*/
            }
            break_:;
        }
        output(otherOutputs, "voxelSize"_, "size"_, [&]{return str(size.x)+"x"_+str(size.y)+"x"_+str(size.z) + " voxels"_;});
    }
};
