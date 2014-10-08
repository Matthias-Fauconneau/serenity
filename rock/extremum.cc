/// \file list.cc Computes lists of positions for each value
#include "sample.h"
#include "volume-operation.h"
#include "thread.h"
#include "crop.h"

/// Computes lists of local extremums
template<Type T, bool maximum> array<short3> extremum(const VolumeT<T>& source) {
    const T* const sourceData = source;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY = X*Y;
    const uint marginX=source.margin.x+1, marginY=source.margin.y+1, marginZ=source.margin.z+1;
    assert_(!source.tiled());
    int64 offsets[26] = { -XY-X-1, -XY-X, -XY-X+1, -XY-1, -XY, -XY+1, -XY+X-1, -XY+X, -XY+X+1,
                             -X-1,    -X,    -X+1,    -1,         +1,    +X-1,    +X,    +X+1,
                          +XY-X-1, +XY-X, +XY-X+1, +XY-1, +XY, +XY+1, +XY+X-1, +XY+X, +XY+X+1 };

    array<short3> lists[coreCount];
    parallel(marginZ, Z-marginZ, [&](uint id, uint z) {
        array<short3>& list = lists[id];
        uint const indexZ = z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            uint const indexZY = indexZ + y*X;
            for(uint x=marginX; x<X-marginX; x++) {
                uint const index = indexZY + x;
                const T* const source = sourceData + index;
                T value = source[0];
                if(value) {
                    if(maximum) {
                        for(uint i: range(26))
                            if(value <= source[offsets[i]] && (value != source[offsets[i]] || offsets[i]<0)) goto continue2;
                    } else {
                        for(uint i: range(26))
                            if(source[offsets[i]] && value >= source[offsets[i]] && (value != source[offsets[i]] || offsets[i]<0)) goto continue2;
                    }
                    list << short3(x,y,z);
                }
                continue2:;
            }
        }
    });
    uint size = 0; for(uint id: range(coreCount)) size += lists[id].size;
    array<short3> list (size); // Avoids unnecessary reallocations (i.e copies)
    for(uint id: range(coreCount)) list << lists[id];
    return list;
}

/// Converts lists to a text file formatted as (x y z )*
String toASCII(const buffer<short3>& list) {
    uint size = list.size*(3*5+1);
    String text (size);
    for(uint i: range(list.size)) { short3 p = list[i]; text << dec(p.x,3) << ' ' << dec(p.y,3) << ' ' << dec(p.z,3) << ", "_; }
    text.size -= 2; // Remove trailing ", "
    return text;
}

/// Computes lists of positions of local extremum
struct LocalExtremumList : Operation {
    string parameters() const override { return "minimum"_; }
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        Volume source = toVolume(*inputs[0]);
        buffer<short3> list = args.contains("minimum"_) ? extremum<uint16, false>(source) : extremum<uint16, true>(source);
        outputs[0]->metadata = String("list"_);
        outputs[0]->data = toASCII(list);
    }
};
template struct Interface<Operation>::Factory<LocalExtremumList>;

/// Converts text file formatted as (x y z)* to list
array<short3> parseList(const string& data) {
    array<short3> list;
    for(TextData s(data); s;) { s.whileAny(" "_); list << short3(parse3(s)); }
    return list;
}

void rasterizeIndex(Volume16& target, const ref<short3>& list) {
    const mref<uint16> targetData = target;
    targetData.clear();
    target.maximum = list.size; // 0 = background
    for(uint i: range(list.size)) {
        short3 p = list[i];
        target(p.x, p.y, p.z) = 1+i;
    }
}

/// Writes 1 + list index of each voxel
struct RasterizeIndex : VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint16); }
    size_t outputSize(const Dict&, const ref<const Result*>& inputs, uint index) override {
        int3 size = sampleCountForSize(parse3(inputs[1]->data)); assert_(size);
        return (uint64)size.x*size.y*size.z*outputSampleSize(index);
    }
    virtual void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>&, const ref<Result*>&, const ref<const Result*>& inputs) override {
        int3 size = sampleCountForSize(parse3(inputs[1]->data));
        outputs[0].sampleCount = nextPowerOfTwo(size);
        outputs[0].margin = (outputs[0].sampleCount-size)/2;
        rasterizeIndex(outputs[0], parseList(inputs[0]->data));
    }
};
template struct Interface<Operation>::Factory<RasterizeIndex>;
