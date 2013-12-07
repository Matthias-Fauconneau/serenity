/// \file family.cc Operations on families (pores)
#include "volume-operation.h"
#include "thread.h"
#include "display.h"
#include "matrix.h"

struct Ball { uint64 index; uint16 sqRadius; };
typedef array<Ball> Family;

/// Converts text file formatted as ((x y z r2):( x y z r2)+)*\n to families
array<Family> parseFamilies(const string& data) {
    array<Family> families;
    TextData s(data);
    while(s) {
        Family family;
        for(;;) {
            uint64 index = zOrder(parse3(s)); uint16 sqRadius = s.integer();
            family << Ball{index, sqRadius};
            if(!s || s.match('\n')) break;
            s.skip(" "_);
        }
        families << move(family);
    }
    return families;
}

void rootIndex(Volume16& target, const ref<Family>& families) {
    uint16* const targetData = target;
    clear(targetData,target.size());
    target.maximum = families.size; // 0 = background
    target.maximum++; // maximum = Multiple family index (e.g throats)
    uint i=0; for(const Family& family: families) {
        i++;
        assert(i<1<<16);
        for(Ball ball: family) {
            if(!targetData[ball.index]) targetData[ball.index] = i;
            else targetData[ball.index] = target.maximum; // Multiple family
        }
    }
}
/// Writes root index of each voxel
class(RootIndex, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint16); }
    size_t outputSize(const Dict&, const ref<const Result*>& inputs, uint index) override {
        int3 size = parse3(inputs[1]->data); assert_(size);
        return (uint64)size.x*size.y*size.z*outputSampleSize(index);
    }
    virtual void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>&, const ref<Result*>&, const ref<const Result*>& inputs) override {
        outputs[0].sampleCount = parse3(inputs[1]->data);
        rootIndex(outputs[0],parseFamilies(inputs[0]->data));
    }
};

void colorizeIndex(Volume24& target, const Volume16& source) {
    bgr* const targetData = target;
    clear(targetData,target.size(),{0,0,0});
    bgr colors[target.maximum+1];
    for(uint i: range(target.maximum)) {
        vec3 rgb = HSVtoRGB(float((i*(target.maximum-1)/3)%target.maximum)/float(target.maximum), 1, 1);
        colors[i] = bgr{sRGB(rgb.x),sRGB(rgb.y),sRGB(rgb.z)};
    }
    colors[0]={0,0,0};
    colors[target.maximum]={0xFF,0xFF,0xFF};
    chunk_parallel(source.size(), [&](uint, uint offset, uint size) {
        const uint16* const sourceData = source + offset;
        bgr* const targetData = target + offset;
        for(uint i : range(size)) targetData[i] = colors[sourceData[i]];
    });
    target.maximum = 0xFF;
}
/// Colorizes each index in a different color
class(ColorizeIndex, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(bgr); }
    virtual void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        colorizeIndex(outputs[0], inputs[0]);
    }
};

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

/// Bounds each familiy with an axis-aligned bounding box
class(Bound, Operation) {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        outputs[0]->metadata = String("boxes"_);
        outputs[0]->data = toASCII(bound(parseFamilies(inputs[0]->data)));
    }
};
