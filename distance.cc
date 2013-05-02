#include "distance.h"

template<bool last> void PBEDT(uint32* const target, const uint32* const source, int X, int Y, int Z, uint xStride, uint yStride, uint zStride) {
    for(int z=0; z<Z; z++) {
        const uint32* const sourceZ = source+z*zStride;
        uint32* const targetZ = target+z*zStride;
        for(int y=0; y<Y; y++) {
            struct element { int cx, x, sd; } stackBase[X];
            element* stack=stackBase;
            const uint32* const sourceY = sourceZ+y*yStride;
            uint32* const targetY = targetZ+y*yStride;
            for(int x=0; x<X; x++) {
                int sd = sourceY[x*xStride];
                if(sd < 0xFFFF) {
                    label:
                    if(stack > stackBase) {
                        int cx = (sd > stack->sd) ? (sd-stack->sd) / (2 * (x - stack->x)) : -2;
                        if(cx == stack->cx) *stack={cx,x,sd};
                        else if(cx < stack->cx) { stack--; goto label; }
                        else if(cx < X) *++stack = {cx,x,sd};
                    } else *++stack = {-1,x,sd};
                }
            }
            if(stack == stackBase) continue;
            for(int x=X-1; x>=0; x--) {
                if(x==stack->cx) stack--;
                int d = x * (x - 2*stack->x) + stack->sd;
                targetY[x*xStride] = last ? d : y*y + d;
            }
        }
    }
}

/// Returns a tiled 32bit distance field volume from a 32bit binary segmented volume
void distance(Volume32& target, const Volume32& source) {
    assert(source.sampleSize==sizeof(uint32));
    assert(target.sampleSize==sizeof(uint32));

    uint X = source.x, Y = source.y, Z = source.z;
    PBEDT<false>(target, source, X,Y,Z, 1,X,X*Y);
    PBEDT<false>(target, target,  Y,Z,X, X,X*Y,1);
    PBEDT<true>(target, target,  Z,X,Y, X*Y,1,X);
    target.num = 1, target.den=target.x;
}
