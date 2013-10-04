#include "volume-operation.h"

class(Synthetic, Operation), virtual VolumeOperation {
    const int3 size = 32;

    uint outputSampleSize(uint) override { return 1; }
    size_t outputSize(const Dict&, const ref<Result*>&, uint) override { return (uint64)size.x*size.y*size.z; }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>&) override {
        Volume8& target = outputs.first();
        assert_(target.data.data, target.data.data, target.data.size, target.data.capacity);
        //target.data.size =  (uint64)size.x*size.y*size.z;
        target.sampleCount = size;
        target.field = String("μ"_); // Radiodensity
        target.maximum = 0xFF;
        clear<uint8>(target, target.size(), uint8(0xFF)); // Rock is high density
        struct Sphere { vec3 position; int radius; };
        vec3 center = vec3(size-int3(1))/2.f;
        int radius = size.x/4;
        for(Sphere s: ref<Sphere>{{center-vec3(radius,0,0),radius},{center+vec3(radius,0,0),radius}}) {
            int3 min=clip(int3(0),int3(s.position)-int3(s.radius),target.sampleCount), max=clip(int3(0),int3(ceil(s.position))+int3(s.radius),target.sampleCount);
            for(int z: range(min.z, max.z)) for(int y: range(min.y, max.y)) for(int x: range(min.x, max.x)) {
                if(sq(vec3(x,y,z)-s.position) <= s.radius*s.radius) target(x,y,z) = 0; // Pore is low density
            }
        }
    }
};
