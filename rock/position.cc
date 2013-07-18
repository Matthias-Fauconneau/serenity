#include "volume-operation.h"
#include "time.h"

void featureTransformX(Volume16& target, const Volume16& source) {
    const uint16* const sourceData = source;
    uint16* const targetData = target;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY = X*Y;
    const int64 marginX=source.margin.x-1, marginY=source.margin.y, marginZ=source.margin.z;
    assert(marginX>=0);
    for(uint z: range(marginZ,Z-marginZ)) {
        const uint16* const sourceZ = sourceData + z*XY;
        uint16* const targetZ = targetData + z*X;
        for(uint y: range(marginY,Y-marginY)) {
            const uint16* const b = sourceZ + y*X;
            uint16* const ftX = targetZ + y;
            uint16 g[X];
            if(b[X-1]) g[X-1] = 0;
            else g[X-1] = 0xFFFF;
            for(int x=X-marginX-2; x>=marginX; x--) g[x] = b[x] ? 0 : (1+g[x+1]); // Backward scan
            int previous = g[0];
            ftX[0] = previous;
            for(int x=marginX+1; x<X-marginX; x++) {
                previous = (x-previous <= int(g[x])) ? previous : x+int(g[x]); // Forward scan
                ftX[x*XY] = previous;
            }
        }
    }
    target.maximum = X-1;
}
defineVolumePass(PositionX, uint16, featureTransformX);

void featureTransformY(Volume2x16& target, const Volume16& source) {
    const uint16* const sourceData = source;
    short2* const targetData = target;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z,  XY = X*Y;
    const int64 marginX=source.margin.x, marginY=source.margin.y-1, marginZ=source.margin.z;
    assert(marginY>=0);
    for(uint x: range(marginX,X-marginX)) {
        const uint16* const sourceX = sourceData + x*XY;
        short2* const targetX = targetData + x*X;
        for(uint z: range(marginZ,Z-marginZ)) {
            const uint16* const ftX = sourceX + z*X;
            short2* const ftXY = targetX + z;
#define g(i) sq(x-int(ftX[i]))
#define f(i,u) (sq((i)-(u))+g(u))
#define Sep(i,u) ((sq(u) - sq(i) + g(u) - g(i)) / (2*((u)-(i))))
            int q=0; uint16 s[Y], t[Y]; s[0]=0, t[0]=0;
            for(int u=marginY+1; u<Y-marginY; u++) { // Forward scan
                while(q>=0 && f(int(t[q]),int(s[q]))>f(int(t[q]),u)) q--;
                if(q<0) q=0, s[0]=u;
                else {
                    int w = 1 + Sep(int(s[q]),u);
                    if(w<Y) { q++; s[q]=u; t[q]=w; }
                }
            }
            for(int u=Y-marginY-1; u>=marginY; u--) { // Backward scan
                ftXY[u*XY] = short2{ftX[s[q]], s[q]};
                if(u==t[q]) q--;
            }
#undef g
        }
    }
    target.maximum = max(X-1,Y-1);
}
defineVolumePass(PositionY, short2, featureTransformY);

void featureTransformZ(Volume3x16& target, const Volume2x16& source) {
    const short2* const sourceData = source;
    short3* const targetData = target;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z,  XY = X*Y;
    const int64 marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z-1;
    assert(marginZ>=0);
    for(uint y: range(marginY,Y-marginY)) {
        const short2* const sourceY = sourceData + y*XY;
        short3* const targetY = targetData + y*X;
        for(uint x: range(marginX,X-marginX)) {
            const short2* const ftXY = sourceY + x*X;
            short3* const ftXYZ = targetY + x;
            int q=0; uint16 s[Z], t[Z]; s[0]=0, t[0]=0;
#define g(i) (sq(x-int(ftXY[i].x))+sq(y-int(ftXY[i].y)))
            for(int u=marginZ+1; u<Z-marginZ; u++) { // Forward scan
                while(q>=0 && f(int(t[q]),int(s[q]))>f(int(t[q]),u)) q--;
                if(q<0) q=0, s[0]=u;
                else {
                    int w = 1 + Sep(int(s[q]),u);
                    if(w<Z) q++, s[q]=u, t[q]=w;
                }
            }
            for(int u=Z-marginZ-1; u>=marginZ; u--) { // Backward scan
                ftXYZ[u*XY] = short3{ftXY[s[q]].x, ftXY[s[q]].y, s[q]};
                if(u==t[q]) q--;
            }
        }
    }
}
defineVolumePass(PositionZ, short3, featureTransformZ);

/// Computes distance field from feature transform (for visualization)
void distance(Volume16& target, const Volume3x16& source) {
        uint maximum=0;
        for(int z: range(target.margin.z, target.sampleCount.z-target.margin.z)) {
            for(int y: range(target.margin.y, target.sampleCount.y-target.margin.y)) {
                for(int x: range(target.margin.x, target.sampleCount.x-target.margin.x)) {
                    int3 d3 = int3(x,y,z)-int3(source(x,y,z));
                    uint d = sq(d3);
                    target(x,y,z) = d;
                    maximum = max(maximum, d);
                }
            }
        }
        assert_(maximum<(1u<<(8*target.sampleSize)), maximum);
        target.maximum=maximum;
        target.squared = true;
    }
defineVolumePass(Distance, uint16, distance);
