#include "volume-operation.h"
#include "matrix.h"
#include "sample.h"

/// Parses boxes from a text file formatted as "x y z sx sy sz"
array<mat4> parseBoxes(const string& data) {
    array<mat4> boxes;
    TextData s(data);
    while(s) {
        vec3 origin = parse3f(s); vec3 scale = parse3f(s); s.skip("\n"_);
        mat4 box(scale); box[3] = vec4(origin, 1.f);
        boxes << box;
    }
    return boxes;
}

void rasterizeBox(Volume16& target, const ref<mat4>& boxes) {
    target.clear();
    target.maximum = boxes.size; // 0 = background
    target.maximum++; // maximum = Multiple family index (e.g throats)
    uint i=0; for(const mat4& box: boxes) {
        i++; assert(i<1<<16);
        // Computes world axis-aligned bounding box of object's oriented bounding box
        vec3 A = vec3(-1), B = vec3(1); // Canonical box
        vec3 O = box[3].xyz(), min = O, max = O; // Initialize min/max to origin
        for(int i: range(2)) for(int j: range(2)) for(int k: range(2)) { // Bounds each corner
            vec3 corner = box*vec3((i?A:B).x,(j?A:B).y,(k?A:B).z);
            min=::min(min, corner), max=::max(max, corner);
        }
        mat4 worldToBox = box.inverse();
        for(int z: range(min.z, max.z+1)) for(int y: range(min.y, max.y+1)) for(int x: range(min.x, max.x+1)) {
            vec3 p = worldToBox*vec3(x,y,z);
            if(p>=A && p<B) { // Inside box
                 target(x,y,z) = target(x,y,z) ? target.maximum : i; // Rasterize family index, except for intersection (throats) which gets maximum value
            }
        }
    }
}

/// Writes root index of each voxel
struct RasterizeBox : VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint16); }
    size_t outputSize(const Dict&, const ref<const Result*>& inputs, uint index) override {
        int3 size = parse3(inputs[1]->data); assert_(size);
        return (uint64)size.x*size.y*size.z*outputSampleSize(index);
    }
    virtual void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>&, const ref<Result*>&, const ref<const Result*>& inputs) override {
        outputs[0].sampleCount = parse3(inputs[1]->data);
        rasterizeBox(outputs[0],parseBoxes(inputs[0]->data));
    }
};
template struct Interface<Operation>::Factory<RasterizeBox>;

/// Computes aspect ratio
array<real> aspectRatio(const ref<mat4>& boxes) {
    array<real> aspectRatios (boxes.size);
    for(const mat4& box: boxes) {
        vec3 scale = vec3(box(0,0), box(1,1), box(2,2));
        aspectRatios << max(max(scale.x, scale.y), scale.z) / min(min(scale.x,scale.y),scale.z);
    }
    return aspectRatios;
}

struct AspectRatio : Operation {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        outputs[0]->metadata = String("vector"_);
        outputs[0]->data = toASCII(aspectRatio(parseBoxes(inputs[0]->data)));
    }
};
template struct Interface<Operation>::Factory<AspectRatio>;

/// Computes distance to nearest box
array<real> nearestDistance(const ref<mat4>& boxes) {
    array<real> nearestDistances (boxes.size);
    for(const mat4& box: boxes) {
        vec3 origin = box[3].xyz();
        float distance = inf;
        for(const mat4& b: boxes) {
            vec3 o = b[3].xyz();
            if(o!=origin) distance = min(distance, sq(o-origin));
        }
        nearestDistances << sqrt(distance);
    }
    return nearestDistances;
}

struct NearestDistance : Operation {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        outputs[0]->metadata = String("vector"_);
        outputs[0]->data = toASCII(nearestDistance(parseBoxes(inputs[0]->data)));
    }
};
template struct Interface<Operation>::Factory<NearestDistance>;
