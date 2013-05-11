#include "distance.h"
#include "time.h"
#include "simd.h"

template<bool last> void perpendicularBisectorEuclideanDistanceTransform(Volume32& target, const Volume32& source, uint X, uint Y, uint Z) {
    const uint32* const sourceData = source;
    uint32* const targetData = target;
    clear(targetData, Z*Y*X); //FIXME: clear only margins
    const uint XY = X*Y;
    constexpr uint unroll = 8;
    int marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ;
    assert_((Y-2*marginY)%unroll == 0);
    struct element { uint sd; int16 cx, x; };
    parallel(marginZ, Z-marginZ, [&](uint, uint z) {
        const uint32* const sourceZ = sourceData+z*XY;
        uint32* const targetZ = targetData+z*X;
        for(int y=marginY; y<Y-marginY; y+=unroll) {
            const uint stackSize = X;
            element stacks[unroll*stackSize]; //64K
            int stackIndices[unroll];
            for(int dy=0; dy<unroll; dy++) {
                memory<element> stack (stacks+dy*stackSize, stackSize);
                const uint32* const sourceY = sourceZ+y*X+dy*X;
                int& i = stackIndices[dy]; i=-1;
                for(int x=marginX; x<X-marginX; x++) {
                    uint sd = sourceY[x];
                    if(sd < 0xFFFFFFFF) {
                        label:
                        if(i >= 0) {
                            int cx = sd > stack[i].sd ? (sd-stack[i].sd) / (2 * (x - stack[i].x)) : -2;
                            assert(cx>=-2 && cx<32768);
                            if(cx == stack[i].cx) stack[i]={sd,int16(cx),int16(x)};
                            else if(cx < stack[i].cx) { i--; goto label; }
                            else if(cx < X-marginX) stack[++i] = {sd,int16(cx),int16(x)};
                        } else stack[++i] = {sd,-1,int16(x)};
                    }
                }
            }
            uint32* const targetZY = targetZ+y;
            for(int x=X-1; x>=0; x--) {
                uint32* const targetZYX = targetZY+x*XY;
                for(int dy=0; dy<unroll; dy++) {
                    int& i = stackIndices[dy];
                    memory<element> stack (stacks+dy*stackSize, stackSize);
                    if(x==stack[i].cx) i--;
                    assert_(i>=0);
                    int d = x * (x - 2*stack[i].x) + stack[i].sd;
                    assert(!last || (sqrt(d)<=x && sqrt(d)<=y+dy && sqrt(d)<=z && sqrt(d)<=1024-x && sqrt(d)<=1024-y-dy && sqrt(d)<=1024-z), sqrt(d), x, y+dy, z);
                    targetZYX[dy] = (last ? d : (y+dy)*(y+dy) + d);
                }
            }
        }
    });
    target.squared=true;
}

template void perpendicularBisectorEuclideanDistanceTransform<false>(Volume32& target, const Volume32& source, uint X, uint Y, uint Z);
template void perpendicularBisectorEuclideanDistanceTransform<true>(Volume32& target, const Volume32& source, uint X, uint Y, uint Z);
