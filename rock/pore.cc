#include "volume-operation.h"
#include "thread.h"
#include "display.h"

// FIXME: -> header (shared with link.cc)
struct Family : array<uint64> {
    Family(uint64 root):root(root){}
    uint64 root;
};
extern array<Family> parseFamilies(const string& data);

void colorize(Volume24& target, const Volume16& source, const ref<Family>& families) {
    assert(target.tiled());
    target.maximum=0xFF;
    // Converts skeleton to grayscale
    chunk_parallel(source.size(), [&](uint, uint offset, uint size) {
        const uint16* const sourceData = source + offset;
        bgr* const targetData = target + offset;
        for(uint i : range(size)) {
            uint8 c = 0xFF*sourceData[i]/source.maximum;
            targetData[i] = bgr{c,c,c};
        }
    });
    bgr* const targetData = target;
    for(uint i : range(families.size)) { const Family& family = families[i];
        vec3 rgb = HSVtoRGB(float(i)/float(families.size), 1, 1);
        bgr c = bgr{sRGB(rgb.x),sRGB(rgb.y),sRGB(rgb.z)};
        log(i, c.b, c.g, c.r, family.size);
        for(uint64 index: family) targetData[index] = c;
    }
}
/// Colorizes skeleton using pore families
class(ColorizeSkeleton, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(bgr); }
    virtual void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs, const mref<Result*>&, const ref<Result*>& otherInputs) override {
        colorize(outputs[0],inputs[0],parseFamilies(otherInputs[0]->data));
    }
};
