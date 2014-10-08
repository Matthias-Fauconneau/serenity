/// \file family.cc Operations on families (pores)
#include "volume-operation.h"
#include "thread.h"
#include "matrix.h"
#include "time.h"
#include "crop.h"

struct Ball { uint64 index; uint16 sqRadius; };
typedef array<Ball> Family;

/// Converts text file formatted as (value:( x y z r2)+)* to families
array<Family> parseFamilies(const string& data) {
    array<Family> families;
    TextData s(data);
    while(s) {
        Family family;
        for(;;) {
            s.whileAny(" ,"_);
            uint64 index = zOrder(parse3(s)); uint16 sqRadius = s.integer();
            family << Ball{index, sqRadius};
            s.whileAny(" ,"_); if(!s || s.match('\n')) break;
        }
        families << move(family);
    }
    return families;
}

void rootIndex(Volume32& target, const ref<Family>& families) {
    const mref<uint32> targetData = target;
    targetData.clear();
    interleavedLookup(target);
    target.maximum = families.size; // 0 = background
    target.maximum++; // maximum = Multiple family index (e.g throats)
    uint i=0; for(const Family& family: families) {
        i++;
        for(Ball ball: family) {
            assert_(ball.index < targetData.size, ball.index, target.size(), target.sampleCount, i, families.size, ball.sqRadius);
            if(!targetData[ball.index]) targetData[ball.index] = i;
            else targetData[ball.index] = target.maximum; // Multiple family
        }
    }
}

/// Writes root index of each voxel
struct RootIndex : VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint32); }
    size_t outputSize(const Dict&, const ref<const Result*>& inputs, uint index) override {
        int3 sampleCount = sampleCountForSize(parse3(inputs[1]->data)); assert_(sampleCount);
        return (uint64)sampleCount.x*sampleCount.y*sampleCount.z*outputSampleSize(index);
    }
    virtual void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>&, const ref<Result*>&, const ref<const Result*>& inputs) override {
        int3 sampleCOunt = sampleCountForSize(parse3(inputs[1]->data));
        outputs[0].sampleCount = nextPowerOfTwo(sampleCOunt);
        outputs[0].margin = (outputs[0].sampleCount - sampleCOunt) / 2;
        rootIndex(outputs[0], parseFamilies(inputs[0]->data));
    }
};
template struct Interface<Operation>::Factory<RootIndex>;

/// Converts boxes to a text file formatted as "x y z sx sy sz"
String toASCII(const ref<mat4>& boxes) {
    String text (boxes.size*6*4);
    for(const mat4& box: boxes)
        text << ftoa(box(0,3),1)+" "_+ftoa(box(1,3),1)+" "_+ftoa(box(2,3),1)+" "_+ftoa(box(0,0),1)+" "_+ftoa(box(1,1),1)+" "_+ftoa(box(2,2),1)+"\n"_;
    return text;
}

array<mat4> bound(const ref<Family>& families) {
    array<mat4> bounds (families.size);
    for(const Family& family: families) {
        int3 min = zOrder(family.first().index)-int3(sqrt((float)family.first().sqRadius));
        int3 max = zOrder(family.first().index)+int3(sqrt((float)family.first().sqRadius));
        for(Ball ball: family) {
            min=::min(min, zOrder(ball.index)-int3(sqrt((float)ball.sqRadius)));
            max=::max(max, zOrder(ball.index)+int3(sqrt((float)ball.sqRadius)));
        }
        vec3 origin = vec3(min+max)/2.f, scale = vec3(max-min)/2.f;
        mat4 box(scale); box[3] = vec4(origin, 1.f);
        bounds << box;
    }
    return bounds;
}

/// Bounds each family with an axis-aligned bounding box
struct Bound : Operation {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        outputs[0]->metadata = String("boxes"_);
        outputs[0]->data = toASCII(bound(parseFamilies(inputs[0]->data)));
    }
};
template struct Interface<Operation>::Factory<Bound>;
