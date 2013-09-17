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
        array<short3>& list = lists[value];
        while(s && s.peek()!='[') {
            uint x=s.integer(); s.skip();
            uint y=s.integer(); s.skip();
            uint z=s.integer(); s.skip();
            list << short3(x,y,z);
        }
    }
    return lists;
}

struct Node {
    //Node(short3 position):position(position){}
    short3 position;
    array<short3> roots; //FIXME: Use a pointer into a pool of root sets
};

array<Node> cluster(/*Volume16& target,*/ const Volume16& source, buffer<array<short3>> lists) {
    assert_(source.tiled());
    const uint16* const sourceData = source;
    /*uint16* const targetData = target;
    rawCopy(targetData, sourceData, source.size());*/
    const int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    const uint64* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    array<Node> nodes;
    for(int R2=lists.size-1; R2>=0; R2--) { // Process balls from largest to smallest
        const array<short3>& balls = lists[R2];
        float R1 = sqrt((float)R2);
        int R = ceil(R1);
        for(short3 P: balls) {
            log(R1, P, nodes.size);
            array<short3> roots;
            for(Node& node: nodes) if(node.position==P) { roots=copy(node.roots); break; }
            if(!roots) roots << P; // Current node is a root
            for(int z: range(max(0,P.z-2*R),min(Z,P.z+2*R))) {
                const uint16* sourceZ = sourceData + offsetZ[z];
                for(int y: range(max(0,P.y-2*R),min(Y,P.y+2*R))) {
                    const uint16* sourceZY = sourceZ + offsetY[y];
                    for(int x: range(max(0,P.x-2*R),max(X,P.x+2*R))) { // Scans voxels for overlapping balls
                        const uint16* sourceZYX = sourceZY + offsetX[x];
                        int d2 = sq(x-P.x)+sq(y-P.y)+sq(z-P.z);
                        float d = sqrt((float)d2);
                        if(d2>4*R2) continue; //>=? By processing from large to small, no overlapping ball can be outside a 2R radius
                        uint16 r2 = sourceZYX[0];
                        if(!r2) continue; // Background
                        float r = sqrt((float)r2);
                        if(r2>R2) continue; // Only adds smaller (or equal) balls
                        if(d>R1+r) continue; // Overlaps when d<R+r
                        for(Node& node: nodes) if(node.position==short3(x,y,z)) { // FIXME: use a volume of array<Node> ?
                            for(short3 root: roots) if(!node.roots.contains(root)) node.roots << P;
                            goto break_;
                        }
                        nodes << Node{short3(x,y,z),copy(roots)};
                        break_:;
                    }
                }
            }
        }
    }
    return nodes;
}

/// Converts parents to a text file formatted as (x y z:( x y z)+)*\n
String toASCII(const array<Node>& nodes) {
    uint size = 0; // Estimates data size to avoid unnecessary reallocations
    for(const Node& node: nodes) if(node.roots.size) size += (3*5+2) + (node.roots.size)*(3*5+1);
    String text (size);
    for(const Node& node: nodes) { // Sort values in descending order
        const array<short3>& list = node.roots;
        if(!list.size) continue;
        text << str(node.position)+":"_;
        for(uint i: range(list.size)) { short3 p = list[i]; text << ' ' << dec(p.x,3) << ' ' << dec(p.y,3) << ' ' << dec(p.z,3); }
        text << "\n"_;
    }
    return text;
}

/// Recursively parents each balls with all smaller overlapping balls
/*class(Cluster, Operation), virtual VolumeOperation {
    //uint outputSampleSize(uint) override { return sizeof(uint16); }
    virtual void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs, const mref<Result*>& otherOutputs, const ref<Result*>& otherInputs) override {
        map<short3,array<short3>> parents = cluster(outputs[0], inputs[0], parseLists(otherInputs[0]->data));
        otherOutputs[0]->metadata = String("parents"_);
        otherOutputs[0]->data = toASCII(parents);
    }
};*/
class(Cluster, Operation) {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        array<Node> parents = cluster(toVolume(*inputs[0]), parseLists(inputs[1]->data));
        outputs[0]->metadata = String("parents"_);
        outputs[0]->data = toASCII(parents);
    }
};
