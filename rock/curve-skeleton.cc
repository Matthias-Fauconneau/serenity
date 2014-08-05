#include "volume-operation.h"
#include "thread.h"

struct Template {
    char rule[27] = {};
    char& operator[](int i) { return rule[i]; }
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
                uint reflectedIndex = (reflection[0] ? 3-x: x) + 3 * (reflection[1] ? 3-y: y) + 9 * (reflection[2] ? 3-z: z);
                reflectedTemplate[index] = baseTemplate[reflectedIndex]; // or equivalently reflectedTemplate[reflectedIndex] = baseTemplate[index]
            }
        }
    }

    copy(mref<uint>(target, target.size()), source);

    uint8* const targetData = target;
    const int64 X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z, XY = X*Y;
    const uint marginX=target.margin.x+1, marginY=target.margin.y+1, marginZ=target.margin.z+1;
    do {
        uint deletedPointCount = 0;
        for(uint subiterationIndex: range(8)) {
            array<uint> deletedPoints;
            for(uint z: range(marginZ, Z-marginZ)) {
                uint const indexZ = z*XY;
                for(uint y=marginY; y<Y-marginY; y++) {
                    uint const indexZY = indexZ+y*X;
                    for(uint x=marginX; x<X-marginX; x++) {
                        uint const index = indexZY+x;
                        uint8* const voxel = targetData+index;
                        for(Template t: reflectedTemplates[subiterationIndex]) {

                        }
                        deletedPointCount++;
                    }
                }
            }
        }
    } while(deletedPoints);
    target.margin.x = marginX, target.margin.y = marginY, target.margin.z = marginZ;
}
defineVolumePass(CurveSkeleton, uint16, templateThin);
