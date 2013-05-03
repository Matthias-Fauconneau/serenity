#include "distance.h"
#include "time.h"
#include "simd.h"

template<bool last> void PBEDT(uint32* const target, const uint32* const source, uint X, uint Y, uint Z) {
    const uint XY = X*Y;
    parallel<>(Z, [&](uint z) {
        const uint32* const sourceZ = source+z*XY;
        uint32* const targetZ = target+z*X;
        constexpr uint unroll = 8; // 2x faster for some reason
        for(int64 y=0; y<Y; y+=unroll) {
            struct element { int64 cx, x, sd; } stacks[unroll][X];
            uint stackIndices[unroll]={};
            for(int64 dy=0; dy<unroll; dy++) {
                element* const stack = stacks[dy];
                const uint32* const sourceY = sourceZ+y*X+dy*X;
                uint& i = stackIndices[dy];
                for(int x=0; x<X; x++) {
                    int64 sd = sourceY[x];
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
            }
            uint32* const targetY = targetZ+y;
            for(int64 x=X-1; x>=0; x--) {
                for(int64 dy=0; dy<unroll; dy++) {
                    uint& i = stackIndices[dy];
                    if(x==stacks[dy][i].cx) i--;
                    int64 d = x * (x - 2*stacks[dy][i].x) + stacks[dy][i].sd;
                    targetY[x*XY+dy] = (last ? d : (y+dy)*(y+dy) + d);
                }
            }
        }
    });
}

/// Returns a tiled 32bit distance field volume from a 32bit binary segmented volume
void distance(Volume32& target, const Volume32& source) {
    const uint X = source.x, Y = source.y, Z = source.z;
    Volume32 buffer(X,Y,Z);
    Time time;
    PBEDT<false>(target, source, X,Y,Z);
    log("distance X", time.reset());
    PBEDT<false>(buffer, target,  Y,Z,X);
    log("distance Y", time.reset());
    PBEDT<true>(target, buffer,  Z,X,Y);
    log("distance Z", time.reset());
    target.num = 1, target.den=maximum(target);
}
