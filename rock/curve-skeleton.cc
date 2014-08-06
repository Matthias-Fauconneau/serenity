#include "volume-operation.h"
#include "thread.h"

struct Template {
    char rule[27] = {};
    char operator[](uint i) const { assert(i<27); return rule[i]; }
    char& operator[](uint i) { assert(i<27); return rule[i]; }
    operator ref<char>() const { return ref<char>(rule, 27); }
};
bool operator==(const Template& a, const Template& b) { return (ref<char>)a == (ref<char>)b; }
string str(const Template& a) { return (ref<char>)a; }

/// Removes voxels matching one of 8 thinning templates until the curve skeleton is found
void templateThin(Volume8& target, const Volume8& source) {

    static char baseTemplates[7][27+1] = { // o: background, O: foreground, .: ignore, x: at least one match
                                           "oooooooooooooOxoxxooooxxoxO",
                                           "ooooooooooxxoOxoxxoxxoxOoxx",
                                           "oooooooooxxxxOxxxxxxxxOxxxx",
                                           "ooooooooooo.oO....oo.ooO.O.",
                                           "oooooo.......O..O.....O....",
                                           "oo.oo........OO.O.....O....",
                                           "ooooooooO....O...O....O...."
                                         };
    array<Template> templates;
    for(int* permutation: (int[][3]){{1,3,9},{3,1,9},{3,9,1},{1,9,3},{9,1,3},{9,3,1}}) {
        for(char* baseTemplate: baseTemplates) {
            Template permutedTemplate;
            for(uint z: range(3)) for(uint y: range(3)) for(uint x: range(3)) {
                uint index = x + 3 * y + 9 * z;
                uint permutedIndex = permutation[0] * x + permutation[1] * y + permutation[2] * z;
                permutedTemplate[index] = baseTemplate[permutedIndex]; // or equivalently permutedTemplate[permutedIndex] = baseTemplate[index]
            }
            templates += permutedTemplate;
        }
    }
    assert_(templates.size == 22);
    Template reflectedTemplates[8][22];
    int reflections[8][3] = {{0,1,0}, {1,0,1}, {0,1,1}, {1,0,0}, {0,0,1}, {1,1,0}, {0,0,0}, {1,1,1}};
    for(uint subiterationIndex: range(8)) {
        int* reflection = reflections[subiterationIndex];
        for(uint templateIndex: range(22)) {
            Template baseTemplate = templates[templateIndex];
            Template& reflectedTemplate = reflectedTemplates[subiterationIndex][templateIndex];
            for(uint z: range(3)) for(uint y: range(3)) for(uint x: range(3)) {
                uint index = x + 3 * y + 9 * z;
                uint reflectedIndex = (reflection[0] ? 2-x: x) + 3 * (reflection[1] ? 2-y: y) + 9 * (reflection[2] ? 2-z: z);
                reflectedTemplate[index] = baseTemplate[reflectedIndex]; // or equivalently reflectedTemplate[reflectedIndex] = baseTemplate[index]
            }
        }
    }

    if(source.tiled()) {
        const ref<uint64> offsetX = source.offsetX, offsetY = source.offsetY, offsetZ = source.offsetZ;
        const uint8* const sourceData = source;
        uint8* const targetData = target;
        const int64 X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z, XY = X*Y;
        parallel(Z, [&](uint, uint z) {
            uint const indexZ = z*XY;
            for(uint y: range(Y)) {
                uint const indexZY = indexZ + y*X;
                for(uint x: range(X)) targetData[indexZY+x] = sourceData[offsetX[x]+offsetY[y]+offsetZ[z]];
            }
        });
        target.offsetX=buffer<uint64>(), target.offsetY=buffer<uint64>(), target.offsetZ=buffer<uint64>();
    } else copy(target.data, source.data);

    uint8* const targetData = target;
    const int64 X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z, XY = X*Y;
    const uint marginX=target.margin.x+1, marginY=target.margin.y+1, marginZ=target.margin.z+1;
    assert_(!target.tiled());
    int64 offsets[27] = { -XY-X-1, -XY-X, -XY-X+1, -XY-1, -XY, -XY+1, -XY+X-1, -XY+X, -XY+X+1,
                             -X-1,    -X,    -X+1,    -1,   0,    +1,    +X-1,    +X,    +X+1,
                          +XY-X-1, +XY-X, +XY-X+1, +XY-1, +XY, +XY+1, +XY+X-1, +XY+X, +XY+X+1 };

    buffer<uint> deletedPoints (target.size(), 0);
    for(;;) {
        uint deletedPointCount = 0;
        for(uint subiterationIndex: range(8)) {
            deletedPoints.size = 0;
            parallel(marginZ, Z-marginZ, [&](uint, uint z) {
                uint const indexZ = z*XY;
                for(uint y=marginY; y<Y-marginY; y++) {
                    uint const indexZY = indexZ + y*X;
                    for(uint x=marginX; x<X-marginX; x++) {
                        uint const index = indexZY + x;
                        uint8* const voxel0 = targetData + index;
                        for(const Template& t: reflectedTemplates[subiterationIndex]) {
                            bool hasX = false; uint match=0;
                            for(uint i: range(27)) {
                                char rule = t[i];
                                char value = voxel0[offsets[i]];
                                /***/  if(rule == 'o' /*background*/) {
                                    if(value != 0) goto continue2;
                                } else if(rule == 'O' /*foreground*/) {
                                    if(value != 1) goto continue2;
                                } else if(rule == '.' /*ignore*/) {
                                } else if(rule == 'x' /*at least one match*/) {
                                    hasX = true;
                                    if(value == 1) match++;
                                } else error(rule);
                            }
                            if(hasX && !match) continue;
                            {size_t i = __sync_fetch_and_add(&deletedPoints.size, 1); // Atomically increments the intersection count
                                assert_(i < deletedPoints.capacity, i, deletedPoints.capacity);
                                deletedPoints[i] = index; // Defers deletion to correctly match neighbour templates
                                __sync_fetch_and_add(&deletedPointCount, 1);
                                break;}
                            continue2:;
                        }
                    }
                }
            });
            for(uint index: deletedPoints) targetData[index] = 0;
        }
        if(!deletedPointCount) break;
    };
    target.margin.x = marginX, target.margin.y = marginY, target.margin.z = marginZ;
}
defineVolumePass(CurveSkeleton, uint8, templateThin);
