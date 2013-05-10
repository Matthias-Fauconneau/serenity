#include "distance.h"
#include "time.h"
#include "simd.h"

template<bool last> void perpendicularBisectorEuclideanDistanceTransform(Volume32& target, const Volume32& source, uint X, uint Y, uint Z) {
    const uint32* const sourceData = source;
    uint32* const targetData = target;
    const uint XY = X*Y;
    constexpr uint unroll = 8; // 2x faster for some reason (write cache?)
    uint margin = max(floor(unroll,source.marginY), (align(unroll,Y)-Y)/2);
    assert_( (Y-2*margin)%unroll == 0 );
    struct element { int cx, x, sd; }; //int64 ?
    static buffer<element> stacks (8*unroll*X);
    assert_(stacks.size == 8*unroll*Y);
    element* const stacksData = stacks.data;
    parallel(Z, [&](uint id, uint z) {
        element* const threadStacks = stacksData+id*unroll*X;
        const uint32* const sourceZ = sourceData+z*XY;
        uint32* const targetZ = targetData+z*X;
        for(int y=margin; y<Y-margin; y+=unroll) {
            int stackIndices[unroll] = {};
            for(int dy=0; dy<unroll; dy++) {
                memory<element> stack (threadStacks+dy*X, X);
                const uint32* const sourceY = sourceZ+y*X+dy*X;
                int& i = stackIndices[dy]; i=-1;
                for(int x=0; x<X; x++) {
                    int sd = sourceY[x];
                    if(sd < 0xFFFFFFFF) {
                        label:
                        if(i >= 0) {
                            int cx = (sd > stack[i].sd) ? (sd-stack[i].sd) / (2 * (x - stack[i].x)) : -2;
                            if(cx == stack[i].cx) stack[i]={cx,x,sd};
                            else if(cx < stack[i].cx) { i--; goto label; }
                            else if(cx < X-margin) stack[++i] = {cx,x,sd};
                        } else stack[++i] = {-1,x,sd};
                    }
                }
            }
            uint32* const targetZY = targetZ+y;
            for(int x=X-1; x>=0; x--) {
                uint32* const targetZYX = targetZY+x*XY;
                for(int dy=0; dy<unroll; dy++) {
                    int& i = stackIndices[dy];
                    memory<element> stack (threadStacks+dy*X, X);
                    if(x==stack[i].cx) i--;
                    assert(i>=0);
                    int d = x * (x - 2*stack[i].x) + stack[i].sd;
                    assert(!last || (sqrt(d)<=x && sqrt(d)<=y+dy && sqrt(d)<=z && sqrt(d)<=1024-x && sqrt(d)<=1024-y-dy && sqrt(d)<=1024-z), sqrt(d), x, y+dy, z);
                    targetZYX[dy] = (last ? d : (y+dy)*(y+dy) + d);
                }
            }
        }
    });
    target.marginY = max(margin, source.marginY); target.squared=true;
}

template void perpendicularBisectorEuclideanDistanceTransform<false>(Volume32& target, const Volume32& source, uint X, uint Y, uint Z);
template void perpendicularBisectorEuclideanDistanceTransform<true>(Volume32& target, const Volume32& source, uint X, uint Y, uint Z);
