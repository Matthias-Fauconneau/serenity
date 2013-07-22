#include "volume-operation.h"
#include "time.h"

constexpr uint width = 8;  // Processes 8 voxel wide stripes to write back 16 bytes at a time

void featureTransformX(Volume16& target, const Volume8& source) {
    const uint8* const sourceData = source;
    uint16* const targetData = target;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    bool tiled = source.tiled(); assert_(tiled);
    const uint64* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    //const int64 marginX=source.margin.x-1, marginY=floor(width/2,source.margin.y), marginZ=source.margin.z;
    assert_(source.margin.x>=1 /*&& (Y-2*marginY)%width == 0*/);
    parallel(0,Z, [&](uint, uint z) {
        const uint8* const sourceZ = sourceData + (tiled ? offsetZ[z] : z*X*Y);
        uint16* const targetZ = targetData + z*Y;
        for(uint y=0; y<Y; y+=width) {
            const uint8* const sourceZY = sourceZ + (tiled ? 0 :  y*X);
            uint16 G[width][X];
            for(uint dy: range(width)) {
                const uint8* const b = sourceZY + (tiled ? offsetY[y+dy] : dy*X);
                uint16* const g = G[dy];
                if(b[tiled ? offsetX[X-1] : X-1]) g[X-1] = 0;
                else g[X-1] = 0xFFFF;
                for(int x=X-2; x>=0; x--) g[x] = b[tiled ? offsetX[x] : x] ? 0 : (1+g[x+1]); // Backward scan
            }
            uint16* const targetZY = targetZ + y;
            int previous[width];
            for(uint dy: range(width)) targetZY[dy*X+0] = previous[dy] = G[dy][0];
            for(int x=1; x<X; x++) { // Forward scan
                for(uint dy: range(width)) targetZY[x*Y*Z+dy] = previous[dy] = (x-previous[dy] <= int(G[dy][x])) ? previous[dy] : x+int(G[dy][x]);
            }
        }
    });
    target.offsetX=buffer<uint64>(), target.offsetY=buffer<uint64>(), target.offsetZ=buffer<uint64>();
    target.maximum = X-1;
}
defineVolumePass(PositionX, uint16, featureTransformX);

void featureTransformY(Volume2x16& target, const Volume16& source) {
    const uint16* const sourceData = source;
    short2* const targetData = target;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    //const int64 marginX=source.margin.x, marginY=source.margin.y-1, marginZ=floor(width/2,source.margin.z);
    assert_(source.margin.y>=1 /*&& (Z-2*marginZ)%width == 0*/);
    parallel(0,X, [&](uint, uint x) {
        const uint16* const sourceX = sourceData + x*Y*Z;
        short2* const targetX = targetData + x*Z;
        for(uint z=0; z<Z; z+=width) {
            const uint16* const sourceXZ = sourceX + z*Y;
            short2* const targetXZ = targetX + z;
#define g(i) sq(x-int(sourceXZ[dz*Y+i]))
#define f(i,u) (sq((i)-(u))+g(u))
#define Sep(i,u) ((sq(u) - sq(i) + g(u) - g(i)) / (2*((u)-(i))))
            int Q[width]; uint16 S[width][Y], T[width][Y];
            for(uint dz: range(width)) {
                int& q = Q[dz];
                uint16* const s = S[dz];
                uint16* const t = T[dz];
                q=0; s[0]=0, t[0]=0;
                for(int u=1; u<Y; u++) { // Forward scan
                    while(q>=0 && f(int(t[q]),int(s[q]))>f(int(t[q]),u)) q--;
                    if(q<0) q=0, s[0]=u;
                    else {
                        int w = 1 + Sep(int(s[q]),u);
                        if(w<Y) { q++; s[q]=u; t[q]=w; }
                    }
                }
            }
            for(int u=Y-1; u>=0; u--) { // Backward scan
                for(uint dz: range(width)) {
                    int& q = Q[dz];
                    uint16* const s = S[dz];
                    uint16* const t = T[dz];
                    targetXZ[u*Z*X+dz] = short2(sourceXZ[dz*Y+s[q]], s[q]);
                    if(u==t[q]) q--;
                }
            }
#undef g
        }
    });
    target.maximum = max(X-1,Y-1);
}
defineVolumePass(PositionY, short2, featureTransformY);

void featureTransformZ(Volume3x16& target, const Volume2x16& source) {
    const short2* const sourceData = source;
    short3* const targetData = target;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    //const int64 marginX=floor(width/2,source.margin.x), marginY=source.margin.y, marginZ=source.margin.z-1;
    assert_(source.margin.z>=1 /*&& (X-2*marginX)%width == 0*/);
    parallel(0,Y, [&](uint, uint y) {
        const short2* const sourceY = sourceData + y*Z*X;
        short3* const targetY = targetData + y*X;
        for(uint x=0; x<X; x+=width) {
            const short2* const sourceYX = sourceY + x*Z;
            short3* const targetYX = targetY + x;
            int Q[width]; uint16 S[width][Z], T[width][Z];
            for(uint dx: range(width)) {
                int& q = Q[dx];
                uint16* const s = S[dx];
                uint16* const t = T[dx];
                q=0; s[0]=0, t[0]=0;
#define g(i) (sq(x+dx-int(sourceYX[dx*Z+(i)].x))+sq(y-int(sourceYX[dx*Z+(i)].y)))
                for(int u=1; u<Z; u++) { // Forward scan
                    while(q>=0 && f(int(t[q]),int(s[q]))>f(int(t[q]),u)) q--;
                    if(q<0) q=0, s[0]=u;
                    else {
                        int w = 1 + Sep(int(s[q]),u);
                        if(w<Z) q++, s[q]=u, t[q]=w;
                    }
                }
            }
            for(int u=Z-1; u>=0; u--) { // Backward scan
                for(uint dx: range(width)) {
                    int& q = Q[dx];
                    uint16* const s = S[dx];
                    uint16* const t = T[dx];
                    targetYX[u*X*Y+dx] = short3(sourceYX[dx*Z+s[q]].x, sourceYX[dx*Z+s[q]].y, s[q]);
                    if(u==t[q]) q--;
                }
            }
        }
    });
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
