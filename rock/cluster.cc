/// \file cluster.cc Computes trees of overlapping balls
#include "list.h"
#include "volume-operation.h"
#include "thread.h"
#include "crop.h"
#include "time.h"

/// Set of balls belonging together (clustered by maximum ball)
typedef array<uint64> Family;
/// Set of families
struct FamilySet {
    array<uint> families; // Family set
    map<uint, uint> unions; // Maps another family set index to the index of the union of this family set and the other family set
    map<uint, array<uint>> complements; // Maps another family set index to the relative complement of this family set in the other family set
};
String str(const FamilySet& o) { return str(o.families,o.unions); }

/// Returns A families and B families intersects
bool intersects(const FamilySet& A, const FamilySet& B) { for(uint family: A.families) if(B.families.contains(family)) return true; return false; }

/// Returns whether A families includes B families
bool includes(const FamilySet& A, const FamilySet& B) { for(uint family: B.families) if(!A.families.contains(family)) return false; return true; }

/// Returns relative complement of set A in B (B \ A)
array<uint> relativeComplement(const array<uint>& A, const array<uint>& B) {
    array<uint> complementSet (B.size);
    for(uint family: B) if(!A.contains(family)) complementSet.append(family);
    return complementSet;
}

/// Adds A and B relative family complements to their relative family complements associative arrays
void relativeComplements(array<FamilySet>& familySets, size_t a, size_t b) {
    FamilySet& A = familySets[a];
    FamilySet& B = familySets[b];
    A.complements.insert(b, relativeComplement(A.families, B.families));
    B.complements.insert(a, relativeComplement(B.families, A.families));
}

array<Family> cluster(Volume32& target, const Volume16& source, buffer<array<short3>> lists, uint minimum) {
    assert_(source.tiled() && target.tiled());

    const mref<uint32> targetData = target;
    targetData.clear();
    const uint16* const sourceData = source;
    const int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    const ref<uint64> offsetX = source.offsetX, offsetY = source.offsetY, offsetZ = source.offsetZ;

    array<Family> families (16);
    array<FamilySet> familySets (sq(16));
    familySets.append(); // family set index 0 is the empty set

    for(int R2=lists.size-1; R2>=0; R2--) { // Process balls from largest to smallest (minimum R2 is not necessarily zero, depends on List.minimum)
        const array<short3>& balls = lists[R2];
        int D2 = 4*R2;
        int D = ceil(sqrt((float)D2));
        float R = sqrt((float)R2);
        log(R, families.size, familySets.size);
        Time time; Time report;
        for(size_t ballIndex: range(balls.size)) {
            if(report/1000>=1) { log(ballIndex,"/",balls.size, ballIndex/(time/1000), "balls/s"); report.reset(); }
            short3 P = balls[ballIndex];
            uint64 parent = offsetZ[P.z] + offsetY[P.y] + offsetX[P.x];
            uint32& parentFamilySetIndex = targetData[parent];
            if(!parentFamilySetIndex) { // parent is a new root
                FamilySet set;
                if(families.size == families.capacity) families.reserve(2*families.size); // Amortizes reallocations
                set.families << families.size; families << Family(ref<uint64>({parent}));
                parentFamilySetIndex = familySets.size; familySets << move(set); // Updates with a new family set lookup index
            }
            for(int z: range(max(0,P.z-D),min(Z,P.z+D+1))) {
                uint64 iZ = offsetZ[z];
                for(int y: range(max(0,P.y-D),min(Y,P.y+D+1))) {
                    uint64 iZY = iZ + offsetY[y];
                    for(int x: range(max(0,P.x-D),min(X,P.x+D+1))) { // Scans voxels for overlapping balls
                        int d2 = sq(x-P.x)+sq(y-P.y)+sq(z-P.z);
                        if(d2 > D2) continue; // By processing from large to small, no overlapping ball can be outside a 2R radius

                        uint64 candidateVertexIndex = iZY + offsetX[x];
                        uint16 r2 = sourceData[candidateVertexIndex];
                        if(r2<=minimum) continue; // Background voxel (or under processing threshold)
                        if(r2>R2) continue; // Only adds smaller (or equal) balls

                        uint32& candidateFamilySetIndex = targetData[candidateVertexIndex]; // Previously assigned index (0 if unprocessed)
                        if(candidateFamilySetIndex==parentFamilySetIndex) continue; // Already assigned to the same family

                        float d = sqrt((float)d2); // Distance between parent and candidate
                        float r = sqrt((float)r2); // Maximal ball radius (0 if background (not a maximal ball))
                        if(d>R+r) continue; // Overlaps when d<=R+r

                        if(!candidateFamilySetIndex) { // Appends candidate vertex index to family
                            for(uint family: familySets[parentFamilySetIndex].families) families[family].append( candidateVertexIndex );
                            candidateFamilySetIndex = parentFamilySetIndex; // Updates last assigned root
                        }
                        else if(familySets[parentFamilySetIndex].unions .values .contains(candidateFamilySetIndex)) {
                            // Already in an union set containing the parent family set
                        }
                        else {
                            uint unionIndex = familySets[parentFamilySetIndex].unions.value(candidateFamilySetIndex, -1);
                            if(unionIndex==uint(-1)) { // Candidate belongs to a family for which no union set with the parent's set exists yet

                                if(includes(familySets[parentFamilySetIndex], familySets[candidateFamilySetIndex])) {
                                    unionIndex = parentFamilySetIndex;  // As the candidate's set is a subset of the parent's set, the union is the superset
                                } else { // Otherwise creates a new union set
                                    FamilySet unionSet;
                                    unionSet.families << familySets[parentFamilySetIndex].families; // Copies the parent's set
                                    unionSet.families += familySets[candidateFamilySetIndex].families; // Adds (without duplicates) the candidate's set
                                    unionIndex = familySets.size; // New index to the union set
                                    if(familySets.size == familySets.capacity) familySets.reserve(2*familySets.size); // Amortizes reallocations
                                    familySets << move(unionSet);
                                }

                                // Registers the union set for later lookups
                                familySets[parentFamilySetIndex].unions.insert(candidateFamilySetIndex, unionIndex);
                                familySets[candidateFamilySetIndex].unions.insert(parentFamilySetIndex, unionIndex);

                                // Creates both relative complements sets (not symmetric) to lookup the set of families to append new elements to
                                relativeComplements(familySets, parentFamilySetIndex, candidateFamilySetIndex);
                            }
                            // Appends the candidate vertex index to the relative complement of the candidate's families in the parent's families
                            for(uint family: familySets[parentFamilySetIndex].complements.at(candidateFamilySetIndex))
                                families[family].append( candidateVertexIndex );
                            // But not the parent vertex index in the candidate's families as this is an asymetric inclusion relation
                            // Sets the candidate family set index to the union set index
                            candidateFamilySetIndex = unionIndex;
                        }
                    }
                }
            }
        }
    }
    return move(families);
}

/// Converts sets to a text file formatted as ((x y z r2)+\n)*
String toASCII(const ref<Family>& families, const Volume16& source) {
    // Estimates text size to avoid unnecessary reallocations
    String target ( sum(apply(families,[](const Family& family){ return family.size*(3*4+5);})) );
    byte* targetPtr = target.begin();
    Time time; log_(str("toASCII",families.size,"families... "_));
    for(const Family& family: families) {
        if(family) {
            for(uint64 index: family) {
                int3 p = zOrder(index);
                itoa<3>(targetPtr, p.x); itoa<3>(targetPtr, p.y); itoa<3>(targetPtr, p.z); itoa<4>(targetPtr, source[index]);
            }
            targetPtr[-1] = '\n';
        }
    }
    log(time);
    target.size = targetPtr-target.begin(); assert(target.size <= target.capacity);
    return target;
}

/// Computes trees of overlapping balls
struct Cluster : VolumeOperation {
    string parameters() const override { return "minimum"_; }
    uint outputSampleSize(uint index) override { return index==0 ? sizeof(uint32) : 0; }
    virtual void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherOutputs,
                         const ref<const Result*>& otherInputs) override {
        otherOutputs[0]->metadata = String("sets"_);
        otherOutputs[0]->data = toASCII(cluster(outputs[0], inputs[0], parseLists(otherInputs[0]->data), args.value("minimum"_,0)), inputs[0]);
    }
};
template struct Interface<Operation>::Factory<Cluster>;
