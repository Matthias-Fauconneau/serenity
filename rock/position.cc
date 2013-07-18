#include "volume-operation.h"
#include "time.h"

void featureTransformX(Volume16& target, const Volume16& source) {
    const uint16* const sourceData = source;
    uint16* const targetData = target;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z,  XY = X*Y;
    for(uint z: range(Z)) {
        const uint16* const sourceZ = sourceData + z*XY;
        uint16* const targetZ = targetData + z*XY;
        for(uint y: range(Y)) {
            const uint16* const b = sourceZ + y*X;
            uint16* const ftX = targetZ + y*X;
            uint16 g[X];
            if(b[X-1]) g[X-1] = 0;
            else g[X-1] = 0xFFFF;
            for(int x=X-2; x>=0; x--) g[x] = b[x] ? 0 : (1+g[x+1]); // Backward scan
            ftX[0] = g[0];
            for(int x=1; x<X; x++) ftX[x] = (x-int(ftX[x-1]) <= int(g[x])) ? ftX[x-1] : x+int(g[x]); // Forward scan
        }
    }
    target.maximum = X-1;
}
defineVolumePass(PositionX, uint16, featureTransformX);

void featureTransformY(Volume2x16& target, const Volume16& source) {
    const uint16* const sourceData = source;
    short2* const targetData = target;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z,  XY = X*Y;
    for(uint z: range(Z)) {
        const uint16* const sourceZ = sourceData + z*XY;
        short2* const targetZ = targetData + z*XY;
        for(int x: range(X)) {
            const uint16* const ftX = sourceZ + x;
            short2* const ftXY = targetZ + x;
#if 0
            for(uint y: range(Y)) ftXY[y*X] = short2{ftX[y*X], (uint16)y};
#else
#define g(i) sq(x-int(ftX[i*X]))
#define f(i,u) (sq((i)-(u))+g(u))
#define Sep(i,u) ((sq(u) - sq(i) + g(u) - g(i)) / (2*((u)-(i))))
            int q=0; uint16 s[Y], t[Y]; s[0]=0, t[0]=0;
            for(int u=1; u<Y; u++) { // Forward scan
                while(q>=0 && f(int(t[q]),int(s[q]))>f(int(t[q]),u)) q--;
                if(q<0) q=0, s[0]=u;
                else {
                    int w = 1 + Sep(int(s[q]),u);
                    if(w<Y) { q++; s[q]=u; t[q]=w; }
                }
            }
            for(int u=Y-1; u>=0; u--) { // Backward scan
                ftXY[u*X] = short2{ftX[s[q]*X], s[q]};
                if(u==t[q]) q--;
            }
#undef g
#endif
        }
    }
}
defineVolumePass(PositionY, short2, featureTransformY);

void featureTransformZ(Volume3x16& target, const Volume2x16& source) {
    const short2* const sourceData = source;
    short3* const targetData = target;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z,  XY = X*Y;
    for(uint y: range(Y)) {
        const short2* const sourceY = sourceData + y*X;
        short3* const targetY = targetData + y*X;
        for(uint x: range(X)) {
            const short2* const ftXY = sourceY + x;
            short3* const ftXYZ = targetY + x;
#if 0
            for(uint z: range(Z)) ftXYZ[z*XY] = short3{ftXY[z*XY].x, ftXY[z*XY].y, (uint16)z};
#else
            int q=0; uint16 s[Z], t[Z]; s[0]=0, t[0]=0;
#define g(i) (sq(x-int(ftXY[i*XY].x))+sq(y-int(ftXY[i*XY].y)))
            for(int u=1; u<Z; u++) { // Forward scan
                while(q>=0 && f(int(t[q]),int(s[q]))>f(int(t[q]),u)) q--;
                if(q<0) q=0, s[0]=u;
                else {
                    int w = 1 + Sep(int(s[q]),u);
                    if(w<Z) q++, s[q]=u, t[q]=w;
                }
            }
            for(int u=Z-1; u>=0; u--) { // Backward scan
                ftXYZ[u*XY] = short3{ftXY[s[q]*XY].x, ftXY[s[q]*XY].y, s[q]};
                if(u==t[q]) q--;
            }
#endif
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
