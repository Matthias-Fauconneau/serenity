#include "distance.h"
#include "time.h"
#include "simd.h"

template<bool last> void PerpendicularBisectorEuclideanDistanceTransform(Volume32& target, const Volume32& source, uint X, uint Y, uint Z) {
    const uint32* const sourceData = source;
    uint32* const targetData = target;
    const uint XY = X*Y;
    constexpr uint unroll = 8; // 2x faster for some reason
    uint margin = (Y-floor(unroll,Y))/2; //FIXME
    assert_( (Y-floor(unroll,Y))%2 == 0 );
    log(margin,Y-margin,Y,Y-2*margin,(Y-2*margin)%8);
    struct element { int64 cx, x, sd; };
    buffer<element> stacks (8*unroll*X); //FIXME: freeing this segfaults for some reason
    element* const stacksData = stacks.data;
    parallel(Z, [sourceData,targetData,X,XY,Y,margin,stacksData](uint id, uint z) { // crashes for some reason
    //for(uint z=0; z<Z; z++) { const uint id=0;
        element* const threadStacks = stacksData+id*unroll*X;
        const uint32* const sourceZ = sourceData+z*XY;
        uint32* const targetZ = targetData+z*X;
        for(int64 y=margin; y<Y-margin; y+=unroll) {
            int stackIndices[unroll];
            for(int64 dy=0; dy<unroll; dy++) {
                element* const stack = threadStacks+dy*X;
                const uint32* const sourceY = sourceZ+y*X+dy*X;
                int& i = stackIndices[dy]; i=0;
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
                    const element* const stack = threadStacks+dy*X;
                    if(x==stack[i].cx) i--;
                    assert(i>=0);
                    int64 d = x * (x - 2*stack[i].x) + stack[i].sd;
                    targetY[x*XY+dy] = (last ? d : (y+dy)*(y+dy) + d);
                }
            }
        }
    });
    target.marginY += margin;
}

template void PerpendicularBisectorEuclideanDistanceTransform<false>(Volume32& target, const Volume32& source, uint X, uint Y, uint Z);
template void PerpendicularBisectorEuclideanDistanceTransform<true>(Volume32& target, const Volume32& source, uint X, uint Y, uint Z);
