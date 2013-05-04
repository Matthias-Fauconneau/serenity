#include "distance.h"
#include "time.h"
#include "simd.h"

template<bool last> void PerpendicularBisectorEuclideanDistanceTransform(Volume32& target, const Volume32& source, uint X, uint Y, uint Z) {
    const uint32* const sourceData = source;
    uint32* const targetData = target;
    const uint XY = X*Y;
    parallel(Z, [&](uint z) {
    //for(uint z=0; z<Z; z++) {
        const uint32* const sourceZ = sourceData+z*XY;
        uint32* const targetZ = targetData+z*X;
        constexpr uint unroll = 8; // 2x faster for some reason
        for(int64 y=0; y<Y; y+=unroll) {
            struct element { int64 cx, x, sd; } stacks[unroll][X];
            int stackIndices[unroll]={};
            for(int64 dy=0; dy<unroll; dy++) {
                element* const stack = stacks[dy];
                const uint32* const sourceY = sourceZ+y*X+dy*X;
                int& i = stackIndices[dy];
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
                    int& i = stackIndices[dy];
                    if(x==stacks[dy][i].cx) i--;
                    assert(i>=0);
                    int64 d = x * (x - 2*stacks[dy][i].x) + stacks[dy][i].sd;
                    targetY[x*XY+dy] = (last ? d : (y+dy)*(y+dy) + d);
                }
            }
        }
    });
}

template void PerpendicularBisectorEuclideanDistanceTransform<false>(Volume32& target, const Volume32& source, uint X, uint Y, uint Z);
template void PerpendicularBisectorEuclideanDistanceTransform<true>(Volume32& target, const Volume32& source, uint X, uint Y, uint Z);
