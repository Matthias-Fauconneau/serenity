#include "thread.h"

struct Template {
    char rule[27] = {};
    char& operator[](int i) { return rule[i]; }
    operator ref<char>() const { return ref<char>(rule, 27); }
};
bool operator==(const Template& a, const Template& b) { return (ref<char>)a == (ref<char>)b; }
string str(const Template& a) { return (ref<char>)a; }
struct Test {
    Test() {
        static char baseTemplates[7][27+1] = { // o: background, O: foreground, .: ignore, x: at least one match
                                               "oooooooooooooOxoxxooooxxoxO",
                                               "ooooooooooxxoOxoxxoxxoxOoxx",
                                               "oooooooooxxxxOxxxxxxxxOxxxx",
                                               "ooooooooooo.oO....oo.ooO.O.",
                                               "oooooo.......O..O.....O....",
                                               "oo.oo........OO.O.....O....",
                                               "ooooooooO....O...O....O...."
                                             };
       // for(uint deletionDirection: {0b010, 0b101, 0b011, 0b100, 0b001, 0b110, 0b000, 0b111}) {
        array<Template> templates;
            for(int* permutations: (int[][3]){{1,3,9},{3,1,9},{3,9,1},{1,9,3},{9,1,3},{9,3,1}}) {
                for(char* baseTemplate: baseTemplates) {
                    Template permutedTemplate;
                    for(uint z: range(3)) for(uint y: range(3)) for(uint x: range(3)) {
                        uint index = x + 3 * y + 9 * z;
                        uint permutedIndex = permutations[0] * x + permutations[1] * y + permutations[2] * z;
                        permutedTemplate[index] = baseTemplate[permutedIndex]; // or equivalently permutedTemplate[permutedIndex] = base[index]
                    }
                    if(!templates.contains(permutedTemplate)) {
                        log(permutedTemplate);
                        templates << permutedTemplate;
                    }
                }
                log(repeat("-"_,27));
            }
            log(templates.size);
        //}
    }
} test;
