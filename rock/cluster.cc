/// \file histogram.cc Histograms volume
#include "sample.h"
#include "volume-operation.h"
#include "thread.h"
#include "crop.h"

/// Converts text file formatted as ([value]:\n(x y z\t)+)* to lists
buffer<array<short3> > parseLists(const string& data) {
    buffer<array<short3>> lists;
    TextData s(data);
    while(s) {
        s.skip("["_); uint value = s.integer(); s.skip("]:\n"_);
        if(!lists) lists = buffer<array<short3>>(value+1, value+1, array<short3>());
        const array<short3>& list = lists[value];
        while(s.peek()!='[') {
            uint x=s.integer(); s.skip();
            uint y=s.integer(); s.skip();
            uint z=s.integer(); s.skip();
            list << short3(x,y,z);
        }
    }
    return lists;
}

map<short3,array<short3> > cluster(Volume16& target, const Volume16& source, buffer<array<short3>> lists) {
    assert_(source.tiled());
    /*uint16* const sourceData = source;
    uint16* const targetData = target;
    rawCopy(targetData, sourceData, source.size());*/
    const uint X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z;
    map<short3,array<short3> > parents;
    for(int R2=lists.size-1; R2>=0; R2--) { // Process balls from largest to smallest
        const array<short3>& balls = lists[R2];
        float R1 = sqrt(R2);
        int R = ceil(R1);
        for(short3 P: balls) {
            for(int z: range(max(0,P.z-2*R),min(Z,P.z+2*R))) {
                for(int y: range(max(0,P.y-2*R),min(Y,P.y+2*R))) {
                    for(int x: range(max(0,P.x-2*R),max(X,P.x+2*R))) { // Scans voxels for overlapping balls
                        int d2 = sq(x-P.x)+sq(y-P.y)+sq(z-P.z);
                        if(d2>4*R2) continue; //>=? By processing from large to small, no overlapping ball can be outside a 2R radius
                        uint16& r2 = target(x,y,z);
                        if(r2>R2) continue; // Only adds smaller (or equal) balls
                        if(sqrt(d2)<R1+sqrt(r2)) { // Overlaps when d<R+r
                            parents[short3(x,y,z)] << P; // Adds large sphere as the overlapping ball parent.
                        }
                    }
                }
            }
        }
    }
}

/// Converts parents to a text file formatted as (x y z:( x y z)+)*\n
String toASCII(const map<short3,array<short3>>& parents) {
    uint size = 0; // Estimates data size to avoid unnecessary reallocations
    for(const array<short3>& list: parents.values) if(list.size) size += (3*5+2) + (list.size)*(3*5+1);
    String text (size);
    for(const_pair<short3, array<short3>> P: parents) { // Sort values in descending order
        const array<short3>& list =  P.value;
        if(!list.size) continue;
        text << str(P.key)+":"_;
        for(uint i: range(list.size)) { short3 p = list[i]; text << ' ' << dec(p.x,3) << ' ' << dec(p.y,3) << ' ' << dec(p.z,3); }
        text << "\n"_;
    }
    return text;
}

/// Recursively parents each balls with all smaller overlapping balls
class(Cluster, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint16); }
    virtual void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const mref<Result*>& otherOutputs, const ref<Result*>& otherInputs) override {
        map<short3,array<short3>> parents = cluster(outputs[0], inputs[0], parseLists(otherInputs[0]->data));
        otherOutputs[0]->metadata = String("parents"_);
        otherOutputs[0]->data = toASCII(parents);
    }
};
