#include "distance.h"
#include "time.h"
#include "simd.h"

template<bool last> void perpendicularBisectorEuclideanDistanceTransform(Volume32& target, const Volume32& source, uint X, uint Y, uint Z) {
    const uint32* const sourceData = source;
    uint32* const targetData = target;
    //uint16* const positionData = position;
    const uint XY = X*Y;
    constexpr uint unroll = 8; // 2x faster for some reason
    uint margin = (Y-floor(unroll,Y))/2;
    assert_( (Y-floor(unroll,Y))%2 == 0 );
    struct element { int cx, x, sd; }; //int64 ?
    static buffer<element> stacks (8*unroll*X);
    assert_(stacks.size == 8*unroll*X);
    element* const stacksData = stacks.data;
    parallel(Z, [&](uint id, uint z) {
        element* const threadStacks = stacksData+id*unroll*X;
        const uint32* const sourceZ = sourceData+z*XY;
        uint32* const targetZ = targetData+z*X;
        //uint16* const positionZ = positionData+z*zStride;
        for(int y=margin; y<Y-margin; y+=unroll) {
            int stackIndices[unroll];
            for(int dy=0; dy<unroll; dy++) {
                element* const stack = threadStacks+dy*X;
                const uint32* const sourceY = sourceZ+y*X+dy*X;
                int& i = stackIndices[dy]; i=0;
                for(int x=0; x<X; x++) {
                    int sd = sourceY[x];
                    if(sd < 0xFFFFFFFF) {
                        label:
                        if(i > 0) {
                            int cx = (sd > stack[i].sd) ? (sd-stack[i].sd) / (2 * (x - stack[i].x)) : -2;
                            if(cx == stack[i].cx) stack[i]={cx,x,sd};
                            else if(cx < stack[i].cx) { i--; goto label; }
                            else if(cx < X) stack[++i] = {cx,x,sd};
                        } else stack[++i] = {-1,x,sd};
                    }
                }
            }
            uint32* const targetZY = targetZ+y;
            //uint16* const positionZY = positionZ+y*yStride;
            for(int x=X-1; x>=0; x--) {
                uint32* const targetZYX = targetZY+x*XY;
                //uint16* const positionZYX = positionZY+x*xStride;
                for(int dy=0; dy<unroll; dy++) {
                    int& i = stackIndices[dy];
                    const element* const stack = threadStacks+dy*X;
                    if(x==stack[i].cx) i--;
                    assert(i>=0);
                    int d = x * (x - 2*stack[i].x) + stack[i].sd;
                    if(last){int radius=sqrt(d); assert_(radius<=x && radius<=y+dy && radius<=z && radius<=1024-x && radius<=1024-y-dy && radius<=1024-z, radius, x, y+dy, z);}
                    targetZYX[dy] = (last ? d : (y+dy)*(y+dy) + d);
                    //positionZYX[dy*yStride] = stack[i].x;
                }
            }
        }
    });
    target.marginY += margin; target.squared=true;
    //position.den = X-1; position.squared=false;
}

template void perpendicularBisectorEuclideanDistanceTransform<false>(Volume32& target, const Volume32& source, uint X, uint Y, uint Z);
template void perpendicularBisectorEuclideanDistanceTransform<true>(Volume32& target, const Volume32& source, uint X, uint Y, uint Z);
