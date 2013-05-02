#include "distance.h"
#include "time.h"

template<bool last> void PBEDT(uint32* const target, const uint32* const source, uint X, uint Y, uint Z, uint xStride, uint yStride, uint zStride) {
    for(uint z=0; z<Z; z++) {
        const uint32* const sourceZ = source+z*zStride;
        uint32* const targetZ = target+z*zStride;
        for(int64 y=0; y<Y; y++) {
            struct element { int64 cx, x, sd; } stack[X];
            uint i=0;
            const uint32* const sourceY = sourceZ+y*yStride;
            uint32* const targetY = targetZ+y*yStride;
            for(int x=0; x<X; x++) {
                int64 sd = sourceY[x*xStride];
                if(sd < 0xFFFFFFFF) {
                    label:
                    if(i > 0) {
                        int64 cx = (sd > stack[i].sd) ? (sd-stack[i].sd) / (2 * (x - stack[i].x)) : -2;
                        if(cx == stack[i].cx) stack[i]={cx,x,sd};
                        else if(cx < stack[i].cx) { i--; goto label; }
                        else if(cx < X) stack[++i] = {cx,x,sd};
                    } else stack[++i] = {-1,x,sd};
                }
            }
            if(i == 0) continue;
            for(int64 x=X-1; x>=0; x--) {
                if(x==stack[i].cx) i--;
                int d = x * (x - 2*stack[i].x) + stack[i].sd;
                targetY[x*xStride] = last ? d : y*y + d;
            }
        }
    }
}

/// Returns a tiled 32bit distance field volume from a 32bit binary segmented volume
void distance(Volume32& target, const Volume32& source) {
    uint X = source.x, Y = source.y, Z = source.z;
    Time time;
    PBEDT<false>(target, source, X,Y,Z, 1,X,X*Y);
    log(time.reset());
    Volume32 buffer(X,Y,Z);
    PBEDT<false>(buffer, target,  Y,Z,X, X,X*Y,1);
    log(time.reset());
    PBEDT<true>(target, buffer,  Z,X,Y, X*Y,1,X);
    log(time.reset());
    target.num = 1, target.den=maximum(target);
    log((Volume&)source, (Volume&)target);
}
