#include "raster.h"

void convert(const Image& target, const ImageH& B, const ImageH& G, const ImageH& R) {
    assert_(target.size == B.size);
    extern uint8 sRGB_forward[0x1000];
    for(size_t i: range(target.ref::size)) {
        B[i] = clamp(0.f, (float)B[i], 1.f);
        G[i] = clamp(0.f, (float)G[i], 1.f);
        R[i] = clamp(0.f, (float)R[i], 1.f);
        //assert_(B[i] >= 0 && B[i] <= 1, (float)B[i]);
        //assert_(G[i] >= 0 && G[i] <= 1, (float)G[i]);
        //assert_(R[i] >= 0 && R[i] <= 1, (float)R[i]);
        target[i] = byte4(sRGB_forward[uint(B[i]*0xFFF)], sRGB_forward[uint(G[i]*0xFFF)], sRGB_forward[uint(R[i]*0xFFF)], 0xFF);
    }
}
