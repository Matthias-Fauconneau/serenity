/// \file list.cc Computes lists of positions for each value
#include "list.h"
#include "sample.h"
#include "volume-operation.h"
#include "thread.h"
#include "crop.h"

/// Computes lists of positions for each value
generic buffer<array<short3> > list(const VolumeT<T>& source, CropVolume crop, uint16 minimum) {
    assert_(crop.min>=source.margin && crop.max <= source.sampleCount-source.margin, source.margin, crop.min, crop.max, source.sampleCount-source.margin);
    uint radiusSq = crop.cylinder ? sq((crop.size.x-1)/2) : -1;
    int2 center = ((crop.min+(crop.max-int3(1)))/2).xy();
    assert_(source.tiled(), "list");
    const ref<uint64> offsetX = source.offsetX, offsetY = source.offsetY, offsetZ = source.offsetZ;
    const ref<T> sourceData = source;
    buffer<array<short3>> lists[coreCount];
    for(uint id: range(coreCount)) lists[id] = buffer<array<short3>>(source.maximum+1, source.maximum+1, 0);
    parallel(crop.min.z, crop.max.z, [&](uint id, uint z) {
        buffer<array<short3>>& list = lists[id];
        const ref<T> sourceZ = sourceData.slice(offsetZ[z]);
        for(int y=crop.min.y; y<crop.max.y; y++) {
            const ref<T> sourceZY = sourceZ.slice(offsetY[y]);
            for(int x=crop.min.x; x<crop.max.x; x++) {
                uint value = sourceZY[offsetX[x]];
                if(uint(sq(x-center.x)+sq(y-center.y)) > radiusSq) continue;
                if(value<=minimum) continue; // Ignores small radii (and background minimum>=0)
                assert(value <= source.maximum, value, source.maximum);
                list[value] << short3(x,y,z);
            }
        }
    }, 1);
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
        for(uint i: range(list.size)) { short3 p = list[i]; text << dec(p.x,3) << ' ' << dec(p.y,3) << ' ' << dec(p.z,3) << ' '; }
        text.last() = '\n'; // Replace trailing space
    }
    return text;
}

/// Converts text file formatted as ([value]:\n(x y z r\t)+)* to lists
buffer<array<short3> > parseLists(const string& data) {
    buffer<array<short3>> lists;
    TextData s(data);
    while(s) {
        s.whileAny(" ,"_); uint value = s.integer(); s.skip(":"_);
        if(!lists) lists = buffer<array<short3>>(value+1, value+1, 0);
        array<short3>& list = lists[value];
        while(s) {
            s.whileAny(" "_); if(s.match('\n')) break;
            s.whileAny(" ,"_); uint x=s.integer();
            s.whileAny(" ,"_); uint y=s.integer();
            s.whileAny(" ,"_); uint z=s.integer();
            list << short3(x,y,z);
        }
    }
    return lists;
}

/// Computes lists of positions for each value
struct List : Operation {
    string parameters() const override { return "cylinder downsample minimum"_; }
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        Volume source = toVolume(*inputs[0]);
        CropVolume crop = parseCrop(args, source.margin, source.sampleCount-source.margin, source.origin);
        buffer<array<short3>> lists;
        /**/  if(source.sampleSize==sizeof(uint16)) lists = list<uint16>(source, crop, args.value("minimum"_,0));
        else if(source.sampleSize==sizeof(uint32)) lists = list<uint32>(source, crop, args.value("minimum"_,0));
        else error(source.sampleSize);
        outputs[0]->metadata = String("lists"_);
        outputs[0]->data = toASCII(lists);
    }
};
template struct Interface<Operation>::Factory<List>;

/// Computes size of each list
array<real> listSize(const buffer<array<short3>>& lists) {
    array<real> listSizes (lists.size);
    for(ref<short3> list: lists) {
        if(!list) continue;
        listSizes << list.size;
    }
    return move(listSizes);
}

/// Computes size of each list
struct ListSize : Operation {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        array<real> listSizes = ::listSize(parseLists(inputs[0]->data));
        outputs[0]->metadata = String("size(index).tsv"_);
        outputs[0]->data = toASCII(UniformSample(listSizes));
    }
};
template struct Interface<Operation>::Factory<ListSize>;
