/// \file list.cc Computes lists of positions for each value
#include "sample.h"
#include "volume-operation.h"
#include "thread.h"
#include "crop.h"

/// Computes lists of positions for each value
buffer<array<short3> > list(const Volume16& source, CropVolume crop, uint16 minimum) {
    assert_(crop.min>=source.margin && crop.max <= source.sampleCount-source.margin, source.margin, crop.min, crop.max, source.sampleCount-source.margin);
    uint radiusSq = crop.cylinder ? sq((crop.size.x-1)/2) : -1;
    int2 center = ((crop.min+(crop.max-int3(1)))/2).xy();
    assert_(source.tiled());
    const ref<uint64> offsetX = source.offsetX, offsetY = source.offsetY, offsetZ = source.offsetZ;
    const ref<uint16> sourceData = source;
    buffer<array<short3>> lists[coreCount];
    for(uint id: range(coreCount)) lists[id] = buffer<array<short3>>(source.maximum+1, source.maximum+1, 0);
    parallel(crop.min.z, crop.max.z, [&](uint id, uint z) {
        buffer<array<short3>>& list = lists[id];
        const ref<uint16> sourceZ = sourceData.slice(offsetZ[z]);
        for(int y=crop.min.y; y<crop.max.y; y++) {
            const ref<uint16> sourceZY = sourceZ.slice(offsetY[y]);
            for(int x=crop.min.x; x<crop.max.x; x++) {
                uint value = sourceZY[offsetX[x]];
                if(uint(sq(x-center.x)+sq(y-center.y)) > radiusSq) continue;
                if(value<=minimum) continue; // Ignores small radii (and background minimum>=0)
                assert(value <= source.maximum, value, source.maximum);
                list[value] << short3(x,y,z);
            }
        }
    });
    buffer<array<short3>> list(source.maximum+1, source.maximum+1, 0);
    for(uint value: range(source.maximum+1)) { // Merges lists
        uint size = 0; for(uint id: range(coreCount)) size += lists[id][value].size;
        list[value].reserve(size); // Avoids unnecessary reallocations (i.e copies)
        for(uint id: range(coreCount)) list[value] << lists[id][value];
    }
    return list;
}

/// Converts lists to a text file formatted as (value:\n(x y z 1\t)+)*
String toASCII(const buffer<array<short3>>& lists) {
    uint size = 0; // Estimates data size to avoid unnecessary reallocations
    for(const array<short3>& list: lists) if(list.size) size += 8 + (list.size)*(3*5+1);
    String text (size);
    for(int value=lists.size-1; value>=0; value--) { // Sort values in descending order
        const array<short3>& list = lists[value];
        if(!list.size) continue;
        text << str(value) << ": "_;
        for(uint i: range(list.size)) { short3 p = list[i]; text << dec(p.x,3) << ' ' << dec(p.y,3) << ' ' << dec(p.z,3) << " 1"_ << (i<list.size-1?" "_:""_); }
        text << "\n"_;
    }
    return text;
}

/// Computes lists of positions for each value
struct List : Operation {
    string parameters() const override { return "cylinder downsample minimum"_; }
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        Volume source = toVolume(*inputs[0]);
        CropVolume crop = parseCrop(args, source.origin+source.margin, source.origin+source.sampleCount-source.margin);
        crop.min -= source.origin, crop.max -= source.origin;
        buffer<array<short3>> lists = list(source, crop, args.value("minimum"_,0));
        outputs[0]->metadata = String("lists"_);
        outputs[0]->data = toASCII(lists);
    }
};
template struct Interface<Operation>::Factory<List>;
