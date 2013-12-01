/// \file family.cc Operations on families (pores)
#include "volume-operation.h"
#include "thread.h"
#include "display.h"
#include "family.h"
#include "matrix.h"

/// Parses 3 integers
int3 parse3(TextData& s) {
    uint x=s.integer(); s.whileAny(" ,x"_);
    uint y=s.integer(); s.whileAny(" ,x"_);
    uint z=s.integer(); s.whileAny(" ,x"_);
    return int3(x,y,z);
}
int3 parse3(const string& data) { TextData s(data); return parse3(s); }

/// Parses 3 decimals
vec3 parse3f(TextData& s) {
    float x=s.decimal(); s.whileAny(" ,x"_);
    float y=s.decimal(); s.whileAny(" ,x"_);
    float z=s.decimal(); s.whileAny(" ,x"_);
    return vec3(x,y,z);
}
vec3 parse3f(const string& data) { TextData s(data); return parse3f(s); }

/// Converts text file formatted as ((x y z):( x y z)+)*\n to families
array<Family> parseFamilies(const string& data) {
    array<Family> families;
    TextData s(data);
    while(s) {
        s.skip("("_); Family family (zOrder(parse3(s))); s.skip("):\n"_); s.skip();
        while(s && s.peek()!='(') { family << zOrder(parse3(s)); s.skip(); }
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
        if(!targetData[family.root]) targetData[family.root] = i;
        else targetData[family.root] = target.maximum; // Multiple family
        for(uint64 index: family) {
            if(!targetData[index]) targetData[index] = i;
            else targetData[index] = target.maximum; // Multiple family
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
        int3 min = zOrder(family.root), max = zOrder(family.root);
        for(uint64 index: family) {
            min=::min(min, zOrder(index));
            max=::max(max, zOrder(index));
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
    uint16* const targetData = target;
    clear(targetData,target.size());
    target.maximum = boxes.size; // 0 = background
    target.maximum++; // maximum = Multiple family index (e.g throats)
    uint i=0; for(const mat4& box: boxes) {
        i++; assert(i<1<<16);
#if OOB //FIXME
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
#else
        vec3 origin = box[3].xyz(); vec3 scale = vec3(box(0,0), box(1,1), box(2,2));
        log(origin, scale);
        vec3 min = origin-scale, max = origin+scale;
        for(int z: range(min.z, max.z+1)) for(int y: range(min.y, max.y+1)) for(int x: range(min.x, max.x+1)) {
            target(x,y,z) = target(x,y,z) ? target.maximum : i; // Rasterize family index, except for intersection (throats) which gets maximum value
        }
#endif
    }
}

/// Writes root index of each voxel
class(RasterizeBox, Operation), virtual VolumeOperation {
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
