#include "volume-operation.h"
#include "time.h"
#include "simd.h"

void featureTransform(Volume3x16& target, const Volume16& source) {
    {
        const uint16* const sourceData = source;
        short3* const targetData = target;
        const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z,  XY = X*Y;
        for(uint z: range(Z)) {
            const uint16* const sourceZ = sourceData + z*XY;
            short3* const targetZ = targetData + z*XY;
            for(uint y: range(Y)) {
                const uint16* const b = sourceZ + y*X;
                short3* const ftX = targetZ + y*X;
                uint16 g[X];
                if(b[X-1]) g[X-1] = 0;
                else g[X-1] = 0xFFFF;
                for(int x=X-2; x>=0; x--) g[x] = b[x] ? 0 : (1+g[x+1]); // Backward scan
                ftX[0].x = g[0];
                for(int x=1; x<X; x++) ftX[x].x = (x-int(ftX[x-1].x) <= int(g[x])) ? ftX[x-1].x : x+int(g[x]); // Forward scan
            }
        }
    }

    {
        short3* const targetData = target;
        const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z,  XY = X*Y;
        for(uint z: range(Z)) {
            short3* const targetZ = targetData + z*XY;
            for(int x: range(X)) {
                short3* const ftXY = targetZ + x;
#define g(i) sq(x-int(ftXY[i*X].x))
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
                    ftXY[u*X].x = ftXY[s[q]*X].x;
                    ftXY[u*X].y = s[q];
                    if(u==t[q]) q--;
                }
#undef g
            }
        }
    }

    {
        short3* const targetData = target;
        const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z,  XY = X*Y;
        for(uint y: range(Y)) {
            short3* const targetY = targetData + y*X;
            for(uint x: range(X)) {
                short3* const ftXYZ = targetY + x;
                int q=0; uint16 s[Z], t[Z]; s[0]=0, t[0]=0;
#define g(i) (sq(x-int(ftXYZ[i*XY].x))+sq(y-int(ftXYZ[i*XY].y)))
                for(int u=1; u<Z; u++) { // Forward scan
                    while(q>=0 && f(int(t[q]),int(s[q]))>f(int(t[q]),u)) q--;
                    if(q<0) q=0, s[0]=u;
                    else {
                        int w = 1 + Sep(int(s[q]),u);
                        if(w<Z) q++, s[q]=u, t[q]=w;
                    }
                }
                for(int u=Z-1; u>=0; u--) { // Backward scan
                    ftXYZ[u*XY].x = ftXYZ[s[q]*XY].x;
                    ftXYZ[u*XY].y = ftXYZ[s[q]*XY].y;
                    ftXYZ[u*XY].z = s[q];
                    if(u==t[q]) q--;
                }
            }
        }
        target.maximum = max(max(X,Y),Z)-1;
    }
}
defineVolumePass(Position, short3, featureTransform);

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
