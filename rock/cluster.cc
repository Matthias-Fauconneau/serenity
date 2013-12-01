/// \file cluster.cc Computes trees of overlapping balls
#include "sample.h"
#include "volume-operation.h"
#include "thread.h"
#include "crop.h"

typedef array<uint64> Family;

/// Converts text file formatted as ([value]:\n(x y z\t)+)* to lists
buffer<array<short3>> parseLists(const string& data) {
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

struct FamilySet {
    array<Family*> families; // Family set
    map<uint,uint> unions; // Maps an other family set index to the index of the union of this family set and the other family set.
    map<uint,uint> complements; // Maps an other family set index to the index of the relative complement of this family set in the other family set.
};
String str(const FamilySet& o) { return str(o.families,o.unions); }

array<unique<Family>> cluster(Volume32& target, const Volume16& source, buffer<array<short3>> lists, uint minimum) {
    uint32* const targetData = target;
    clear(targetData, target.size());

    assert_(source.tiled() && target.tiled());
    const uint16* const sourceData = source;
    const int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    const uint64* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    array<unique<Family>> families;
    array<unique<FamilySet>> familiesLookup; familiesLookup<<unique<FamilySet>(); // index 0 is no families (background or yet unassigned)
    for(int R2=lists.size-1; R2>=0; R2--) { // Process balls from largest to smallest
        const array<short3>& balls = lists[R2];
        float R = sqrt((float)R2);
        int D = ceil(2*R);
        log(R, families.size, familiesLookup.size);
        for(short3 P: balls) {
            uint64 parent = offsetZ[P.z] + offsetY[P.y] + offsetX[P.x];
            uint32& parentIndex = targetData[parent];
            if(!parentIndex) { // parent is a new root
                families << unique<Family>(ref<uint64>{parent});
                unique<FamilySet> set; set->families << families.last().pointer;
                parentIndex = familiesLookup.size; // New family set lookup index
                familiesLookup << move(set);
            }
            FamilySet& parentSet = familiesLookup[parentIndex];
            const array<Family*>& parentFamilies = parentSet.families;
            map<uint,uint>& unions = parentSet.unions;
            map<uint,uint>& complements = parentSet.complements;
            for(int z: range(max(0,P.z-D),min(Z,P.z+D+1))) {
                uint64 iZ = offsetZ[z];
                for(int y: range(max(0,P.y-D),min(Y,P.y+D+1))) {
                    uint64 iZY = iZ + offsetY[y];
                    for(int x: range(max(0,P.x-D),min(X,P.x+D+1))) { // Scans voxels for overlapping balls
                        uint64 index = iZY + offsetX[x];
                        uint16 r2 = sourceData[index]; float r = sqrt((float)r2); // Maximal ball radius (0 if background (not a maximal ball))
                        uint32& otherIndex = targetData[index]; // Previously assigned index (0 if unprocessed)
                        int d2 = sq(x-P.x)+sq(y-P.y)+sq(z-P.z); float d = sqrt((float)d2); // Distance between parent and candidate
                        if(r2<=minimum) continue; // Background voxel (or under processing threshold)
                        if(otherIndex==parentIndex) continue; // Already assigned to the same family
                        if(d2>=4*R2) continue; // By processing from large to small, no overlapping ball can be outside a 2R radius
                        if(r2>R2) continue; // Only adds smaller (or equal) balls
                        if(d>=R+r) continue; // Overlaps when d<R+r
                        if(!otherIndex) { // Appends to family
                            otherIndex = parentIndex; // Updates last assigned root
                            for(Family* family: parentFamilies) family->append( index );
                        }
                        else if(unions.values.contains(otherIndex)) {} // Already in an union set containing the parent family set
                        else {
                            int i = unions.keys.indexOf(otherIndex);
                            if(i>=0) { // In a family for which a union set with the parent already exists
                                int complementIndex = complements.at(otherIndex);
                                for(Family* family: familiesLookup[complementIndex]->families) family->append( index ); // Appends to the complement
                                int unionIndex = unions.values[i];
                                otherIndex = unionIndex; // Updates to the union set index
                            } else { // In a family for which no union set exists yet
                                FamilySet& otherSet = familiesLookup[otherIndex];
                                uint unionIndex = parentIndex;
                                for(Family* family: otherSet.families) {
                                    if(!parentFamilies.contains(family)) { // Creates a new set only if other is not included in this
                                        unique<FamilySet> unionSet;
                                        unionSet->families << parentSet.families; // Copies the parent set
                                        unionSet->families += otherSet.families; // Adds (without duplicates) the other set
                                        unionIndex = familiesLookup.size; // New lookup index to the union family set
                                        familiesLookup << move(unionSet);
                                        break;
                                    }
                                } //else No-op union as other set is included in parent set
                                parentSet.unions.insert(otherIndex, unionIndex);
                                otherSet.unions.insert(parentIndex, unionIndex);

                                // Creates both relative complements sets (not commutative) to lookup the set of families to append new elements to
                                { // Relative complement of parent in other (other \ parent)
                                    uint complementIndex = 0;
                                    for(Family* family: otherSet.families) {
                                        if(!parentSet.families.contains(family)) { // Creates a new set only if other is not included in parent
                                            unique<FamilySet> complementSet; // Relative complement of parent in other (other \ parent)
                                            complementSet->families << otherSet.families; // Copies the other set
                                            complementSet->families.filter([&parentSet](const Family* f){ return parentSet.families.contains(f); }); // Removes the parent set
                                            complementIndex = familiesLookup.size; // New lookup index to the union family set
                                            familiesLookup << move(complementSet);
                                            break;
                                        }
                                    } //else Empty complement as other set is included in parent set
                                    parentSet.complements.insert(otherIndex, complementIndex);
                                }
                                { // Relative complement of other in parent (parent \ other)
                                    uint complementIndex = 0;
                                    for(Family* family: parentSet.families) {
                                        if(!otherSet.families.contains(family)) { // Creates a new set only if parent is not included in other
                                            unique<FamilySet> complementSet; // Relative complement of parent in other (other \ parent)
                                            complementSet->families << parentSet.families; // Copies the parent set
                                            complementSet->families.filter([&otherSet](const Family* f){ return otherSet.families.contains(f); }); // Removes the other set
                                            complementIndex = familiesLookup.size; // New lookup index to the union family set
                                            familiesLookup << move(complementSet);
                                            break;
                                        }
                                    } //else Empty complement as other set is included in parent set
                                    otherSet.complements.insert(parentIndex, complementIndex);
                                }
                                otherIndex = unionIndex;
                            }
                        }
                    }
                }
            }
        }
    }
    return families;
}

/// Converts families to a text file formatted as ((x y z r2)+\n)*
String toASCII(const ref<unique<Family>>& families, const Volume16& source) {
    String text ( sum(apply(families,[](const Family& family){ return family.size*4*5;})) ); // Estimates text size to avoid unnecessary reallocations
    for(const Family& family: families) {
        for(uint64 index: family) {
            int3 p = zOrder(index);
            text << dec(p.x,3) << ' ' << dec(p.y,3) << ' ' << dec(p.z,3) << ' ' << dec(source[index],3) << ' ';
        }
        text.last() = '\n';
    }
    return text;
}

/// Computes trees of overlapping balls
class(Cluster, Operation), virtual VolumeOperation {
    string parameters() const override { return "minimum"_; }
    uint outputSampleSize(uint index) override { return index==0 ? sizeof(uint32) : 0; }
    virtual void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherOutputs, const ref<const Result*>& otherInputs) override {
        array<unique<Family>> families = cluster(outputs[0], inputs[0], parseLists(otherInputs[0]->data), args.value("minimum"_,0));
        otherOutputs[0]->metadata = String("families"_);
        otherOutputs[0]->data = toASCII(families, inputs[0]);
    }
};
