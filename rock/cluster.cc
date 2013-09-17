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

struct Family : array<uint64> {
    Family(uint64 root):root(root){}
    uint64 root;
};

array<Family> cluster(VolumeT<uint64>& target, const Volume16& source, buffer<array<short3>> lists, uint minimum) {
    uint64* const targetData = target;
    clear(targetData, target.size());

    assert_(source.tiled() && target.tiled());
    const uint16* const sourceData = source;
    const int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    const uint64* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    array<Family> families;
    for(int R2=lists.size-1; R2>=0; R2--) { // Process balls from largest to smallest
        const array<short3>& balls = lists[R2];
        float R1 = sqrt((float)R2);
        int R = ceil(R1);
        for(short3 P: balls) {
            uint64 parent = offsetZ[P.z] + offsetY[P.y] + offsetX[P.x];
            log(R1, P);
            array<Family*> parentFamilies;
            for(Family& f: families) if(f.contains(parent)) parentFamilies << &f;
            if(!parentFamilies) { families << Family(parent); parentFamilies << &families.last(); } // parent is a root
            if(parentFamilies.size==1) { // Fast path for the common case of a balls belonging to a single family (pores)
                Family* family = parentFamilies.first(); uint64 root = family->root;
                for(int z: range(max(0,P.z-2*R),min(Z,P.z+2*R))) {
                    uint64 iZ = offsetZ[z];
                    for(int y: range(max(0,P.y-2*R),min(Y,P.y+2*R))) {
                        uint64 iZY = iZ + offsetY[y];
                        for(int x: range(max(0,P.x-2*R),max(X,P.x+2*R))) { // Scans voxels for overlapping balls
                            uint64 index = iZY + offsetX[x];
                            uint16 r2 = sourceData[index]; float r = sqrt((float)r2); // Maximal ball radius (0 if background (not a maximal ball))
                            uint16 previousRoot = targetData[index]; // Previously assigned root (0 if unprocessed)
                            int d2 = sq(x-P.x)+sq(y-P.y)+sq(z-P.z); float d = sqrt((float)d2); // Distance between parent and candidate
                            if(r2<=minimum) continue; // Background voxel (or under processing threshold)
                            if(previousRoot==root) continue; // Already assigned to the same family
                            if(d2>4*R2) continue; //>=? By processing from large to small, no overlapping ball can be outside a 2R radius
                            if(r2>R2) continue; // Only adds smaller (or equal) balls
                            if(d>R1+r) continue; // Overlaps when d<R+r
                            targetData[index] = root; // Updates last assigned root
                            if(!family->contains(index)) family->append( index ); // This might be a second connection to the family after inheriting from another root //FIXME: binary search
                        }
                    }
                }
            } else { // Slow path for balls belonging to multiple family (throats)
                array<uint64> roots; for(const Family* family: parentFamilies) roots << family->root; uint64 root = roots.first();
                for(int z: range(max(0,P.z-2*R),min(Z,P.z+2*R))) {
                    uint64 iZ = offsetZ[z];
                    for(int y: range(max(0,P.y-2*R),min(Y,P.y+2*R))) {
                        uint64 iZY = iZ + offsetY[y];
                        for(int x: range(max(0,P.x-2*R),max(X,P.x+2*R))) { // Scans voxels for overlapping balls
                            uint64 index = iZY + offsetX[x];
                            uint16 r2 = sourceData[index]; float r = sqrt((float)r2); // Maximal ball radius (0 if background (not a maximal ball))
                            uint16 previousRoot = targetData[index]; // Previously assigned root (0 if unprocessed)
                            int d2 = sq(x-P.x)+sq(y-P.y)+sq(z-P.z); float d = sqrt((float)d2); // Distance between parent and candidate
                            if(r2<=minimum) continue; // Background voxel (or under processing threshold)
                            if(roots.contains(previousRoot)) continue; // Already assigned to the same family //FIXME: is this conservative?
                            //if(previousRoot==root) continue; // Already assigned to the same family //FIXME: might be enough
                            if(d2>4*R2) continue; //>=? By processing from large to small, no overlapping ball can be outside a 2R radius
                            if(r2>R2) continue; // Only adds smaller (or equal) balls
                            if(d>R1+r) continue; // Overlaps when d<R+r
                            targetData[index] = root; // Updates last assigned root with the first root of the root sets
                            for(Family* family: parentFamilies) {
                                if(!family->contains(index)) family->append( index ); // This might be another connection //FIXME: binary search
                            }
                        }
                    }
                }
            }
        }
    }
    return families;
}

/// Converts parents to a text file formatted as (x y z:( x y z)+)*\n
String toASCII(const array<Family>& families) {
    uint size = 0; // Estimates data size to avoid unnecessary reallocations
    for(const Family& family: families) /*if(family.size)*/ size += (3*5+2) + (family.size)*(3*5+1);
    String text (size);
    for(const Family& family: families) {
        //if(!family.size) continue; // Might be an isolated root (also includes them)
        int3 position = zOrder(family.root); // Convert back Z-order index to position
        text << str(position)+":\n"_;
        for(uint i: range(family.size)) { int3 p = zOrder(family[i]); text << dec(p.x,3) << ' ' << dec(p.y,3) << ' ' << dec(p.z,3) << ((i+1)%16?"  "_:"\n"_); }
        text << "\n"_;
    }
    return text;
}

/// Computes trees of overlapping balls
class(Cluster, Operation), virtual VolumeOperation {
    string parameters() const override { return "minimum"_; }
    uint outputSampleSize(uint index) override { return index==0 ? sizeof(uint64) : 0; }
    virtual void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const mref<Result*>& otherOutputs, const ref<Result*>& otherInputs) override {
        array<Family> parents = cluster(outputs[0], inputs[0], parseLists(otherInputs[0]->data), args.value("minimum"_,0));
        otherOutputs[0]->metadata = String("families"_);
        otherOutputs[0]->data = toASCII(parents);
    }
};
