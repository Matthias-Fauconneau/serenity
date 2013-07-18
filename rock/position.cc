#include "volume-operation.h"
#include "time.h"
#include "simd.h"

#if 1
void featureTransformX(Volume16& target, const Volume32& source) {
    const uint32* const sourceData = source;
    uint16* const targetData = target;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z,  XY = X*Y;
    for(uint z: range(Z)) {
        const uint32* const sourceZ = sourceData + z*XY;
        uint16* const targetZ = targetData + z*XY;
        for(uint y: range(Y)) {
            const uint32* const b = sourceZ + y*X;
            uint16* const ftX = targetZ + y*X;
            uint16 g[X];
            if(b[X-1]!=0xFFFFFFFF) g[X-1] = 0;
            else g[X-1] = 0xFFFF;
            for(int x=X-2; x>=0; x--) g[x] = b[x]!=0xFFFFFFFF ? 0 : (1+g[x+1]); // Backward scan
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

#else
void setBorders(Volume32& target) {
    const uint64 X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z, XY=X*Y;
    const uint marginX=target.margin.x, marginY=target.margin.y, marginZ=target.margin.z;
    typedef uint32 T;
    T* const targetData = target;
    assert(!target.tiled());
    for(uint z=marginZ; z<Z-marginZ; z++) {
        T* const targetZ = targetData + z*XY;
        for(uint y=0; y<Y; y++) {
            T* const targetZY = targetZ + y*X;
            for(uint x=0; x<marginX; x++) targetZY[x] = x*x;
            for(uint x=X-marginX; x<X; x++) targetZY[x] = x*x;
        }
        for(uint x=0; x<X; x++) {
            T* const targetZX = targetZ + x;
            for(uint y=0; y<marginY; y++) targetZX[y*X] = x*x;
            for(uint y=Y-marginY; y<Y; y++) targetZX[y*X] = x*x;
        }
    }
    for(uint y=0; y<Y; y++) {
        T* const targetY = targetData + y*X;
        for(uint x=0; x<X; x++) {
            T* const targetYX = targetY + x;
            for(uint z=0; z<marginZ; z++) targetYX[z*XY] = x*x;
            for(uint z=Z-marginZ; z<Z; z++) targetYX[(Z-marginZ-1)*XY] = x*x;
        }
    }
}

// FIXME: try to factor these

// X
void perpendicularBisectorEuclideanDistanceTransform(Volume32& target, Volume16& positionX, const Volume32& source) {
    setBorders(target);
    const int64 sX=source.sampleCount.x, sY=source.sampleCount.y, sZ=source.sampleCount.z;
    const int64 tX=sY, tY=sZ;
    const int marginX=source.margin.x-1, marginY=source.margin.y-1, marginZ=source.margin.z-1;
    const uint32* const sourceData = source;
    uint32* const targetData = target;
    uint16* const xPositionData = positionX;
    parallel(marginZ, sZ-marginZ, [&](uint, uint z) {
        const uint32* const sourceZ = sourceData+z*sX*sY;
        uint32* const targetZ = targetData+z*tX;
        uint16* const xPositionZ = xPositionData+z*tX;
        constexpr uint unroll = 8;
        assert(marginX>=0 && marginY>=0 && marginZ>=0 && (sY-2*marginY)%unroll == 0);
        for(int y=marginY; y<sY-marginY; y+=unroll) {
            const uint stackSize = sX;
            struct element { uint sd; int16 cx, x; } stacks[unroll*stackSize];
            int stackIndices[unroll];
            for(uint dy=0; dy<unroll; dy++) {
                mref<element> stack (stacks+dy*stackSize, stackSize);
                const uint32* const sourceY = sourceZ+(y+dy)*sX;
                int& i = stackIndices[dy]; i=-1;
                for(int x=marginX; x<sX-marginX; x++) {
                    uint sd = sourceY[x];
                    if(sd < 0xFFFFFFFF) {
                        label:
                        if(i >= 0) {
                            int cx = sd > stack[i].sd ? (sd-stack[i].sd) / (2 * (x - stack[i].x)) : -2;
                            assert(cx>=-2 && cx<32768);
                            if(cx == stack[i].cx) stack[i]={sd,int16(cx),int16(x)};
                            else if(cx < stack[i].cx) { i--; goto label; }
                            else if(cx < sX-marginX) stack[++i] = {sd,int16(cx),int16(x)};
                        } else stack[++i] = {sd,-1,int16(x)};
                    }
                }
            }
            uint32* const targetZY = targetZ+y;
            uint16* const xPositionZY = xPositionZ+y;
            for(int x=sX-marginX-1; x>=marginX; x--) {
                uint32* const targetZYX = targetZY+x*tX*tY;
                uint16* const xPositionZYX = xPositionZY+x*tX*tY;
                for(uint dy=0; dy<unroll; dy++) {
                    int& i = stackIndices[dy];
                    mref<element> stack (stacks+dy*stackSize, stackSize);
                    if(x==stack[i].cx) i--;
                    assert(i>=0);
                    int sx = stack[i].x;
                    int d = x * (x - 2*sx) + stack[i].sd;
                    targetZYX[dy] = (y+dy)*(y+dy) + d;
                    xPositionZYX[dy] = sx;
                }
            }
        }
    });
    target.squared=true;
    positionX.squared=false, positionX.maximum=sX-1;
}

// Y
void perpendicularBisectorEuclideanDistanceTransform(Volume32& target, Volume16& positionX, Volume16& positionY, const Volume32& source, const Volume16& sourceX) {
    setBorders(target);
    const int64 sX=source.sampleCount.x, sY=source.sampleCount.y, sZ=source.sampleCount.z;
    const int64 tX=sY, tY=sZ;
    const int marginX=source.margin.x-1, marginY=source.margin.y-1, marginZ=source.margin.z-1;
    const uint32* const sourceData = source;
    const uint16* const xSourceData = sourceX;
    uint32* const targetData = target;
    uint16* const xPositionData = positionX;
    uint16* const yPositionData = positionY;
    parallel(marginZ, sZ-marginZ, [&](uint, uint z) {
        const uint32* const sourceZ = sourceData+z*sX*sY;
        const uint16* const xSourceZ = xSourceData+z*sX*sY;
        uint32* const targetZ = targetData+z*tX;
        uint16* const xPositionZ = xPositionData+z*tX;
        uint16* const yPositionZ = yPositionData+z*tX;
        constexpr uint unroll = 8;
        assert(marginX>=0 && marginY>=0 && marginZ>=0 && (sY-2*marginY)%unroll == 0);
        for(int y=marginY; y<sY-marginY; y+=unroll) {
            const uint stackSize = sX;
            struct element { uint sd; int16 cx, x; } stacks[unroll*stackSize];
            int stackIndices[unroll];
            for(uint dy=0; dy<unroll; dy++) {
                mref<element> stack (stacks+dy*stackSize, stackSize);
                const uint32* const sourceZY = sourceZ+(y+dy)*sX;
                int& i = stackIndices[dy]; i=-1;
                for(int x=marginX; x<sX-marginX; x++) {
                    uint sd = sourceZY[x];
                    if(sd < 0xFFFFFFFF) {
                        label:
                        if(i >= 0) {
                            int cx = sd > stack[i].sd ? (sd-stack[i].sd) / (2 * (x - stack[i].x)) : -2;
                            assert(cx>=-2 && cx<32768);
                            if(cx == stack[i].cx) stack[i]={sd,int16(cx),int16(x)};
                            else if(cx < stack[i].cx) { i--; goto label; }
                            else if(cx < sX-marginX) stack[++i] = {sd,int16(cx),int16(x)};
                        } else stack[++i] = {sd,-1,int16(x)};
                    }
                }
            }
            const uint16* const xSourceZY = xSourceZ+y*sX;
            uint32* const targetZY = targetZ+y;
            uint16* const xPositionZY = xPositionZ+y;
            uint16* const yPositionZY = yPositionZ+y;
            for(int x=sX-marginX-1; x>=marginX; x--) {
                uint32* const targetZYX = targetZY+x*tX*tY;
                uint16* const xPositionZYX = xPositionZY+x*tX*tY;
                uint16* const yPositionZYX = yPositionZY+x*tX*tY;
                for(uint dy=0; dy<unroll; dy++) {
                    int& i = stackIndices[dy];
                    mref<element> stack (stacks+dy*stackSize, stackSize);
                    if(x==stack[i].cx) i--;
                    assert_(i>=0);
                    int sx = stack[i].x;
                    int d = x * (x - 2*sx) + stack[i].sd;
                    targetZYX[dy] = (y+dy)*(y+dy) + d;
                    xPositionZYX[dy] = xSourceZY[dy*sX + sx];
                    yPositionZYX[dy] = sx;
                }
            }
        }
    });
    target.squared=true;
    positionX.squared=false, positionX.maximum=sourceX.maximum;
    positionY.squared=false, positionY.maximum=sX-1;
}

// Z
void perpendicularBisectorEuclideanDistanceTransform(Volume16& positionX, Volume16& positionY, Volume16& positionZ, const Volume32& source, const Volume16& sourceX, const Volume16& sourceY) {
    //setBorders(target);
    const int64 sX=source.sampleCount.x, sY=source.sampleCount.y, sZ=source.sampleCount.z;
    const int64 tX=sY, tY=sZ;
    const int marginX=source.margin.x-1, marginY=source.margin.y-1, marginZ=source.margin.z-1;
    const uint32* const sourceData = source;
    const uint16* const xSourceData = sourceX;
    const uint16* const ySourceData = sourceY;
    uint16* const xPositionData = positionX;
    uint16* const yPositionData = positionY;
    uint16* const zPositionData = positionZ;
    parallel(marginZ, sZ-marginZ, [&](uint, uint z) {
        const uint32* const sourceZ = sourceData+z*sX*sY;
        const uint16* const xSourceZ = xSourceData+z*sX*sY;
        const uint16* const ySourceZ = ySourceData+z*sX*sY;
        uint16* const xPositionZ = xPositionData+z*tX;
        uint16* const yPositionZ = yPositionData+z*tX;
        uint16* const zPositionZ = zPositionData+z*tX;
        constexpr uint unroll = 8;
        assert(marginX>=0 && marginY>=0 && marginZ>=0 && (sY-2*marginY)%unroll == 0);
        for(int y=marginY; y<sY-marginY; y+=unroll) {
            const uint stackSize = sX;
            struct element { uint sd; int16 cx, x; } stacks[unroll*stackSize];
            int stackIndices[unroll];
            for(uint dy=0; dy<unroll; dy++) {
                mref<element> stack (stacks+dy*stackSize, stackSize);
                const uint32* const sourceZY = sourceZ+y*sX+dy*sX;
                int& i = stackIndices[dy]; i=-1;
                for(int x=marginX; x<sX-marginX; x++) {
                    uint sd = sourceZY[x];
                    if(sd < 0xFFFFFFFF) {
                        label:
                        if(i >= 0) {
                            int cx = sd > stack[i].sd ? (sd-stack[i].sd) / (2 * (x - stack[i].x)) : -2;
                            assert(cx>=-2 && cx<32768);
                            if(cx == stack[i].cx) stack[i]={sd,int16(cx),int16(x)};
                            else if(cx < stack[i].cx) { i--; goto label; }
                            else if(cx < sX-marginX) stack[++i] = {sd,int16(cx),int16(x)};
                        } else stack[++i] = {sd,-1,int16(x)};
                    }
                }
            }
            const uint16* const xSourceZY = xSourceZ+y*sX;
            const uint16* const ySourceZY = ySourceZ+y*sX;
            uint16* const xPositionZY = xPositionZ+y;
            uint16* const yPositionZY = yPositionZ+y;
            uint16* const zPositionZY = zPositionZ+y;
            for(int x=sX-1-marginX; x>=marginX; x--) {
                uint16* const xPositionZYX = xPositionZY+x*tX*tY;
                uint16* const yPositionZYX = yPositionZY+x*tX*tY;
                uint16* const zPositionZYX = zPositionZY+x*tX*tY;
                for(uint dy=0; dy<unroll; dy++) {
                    int& i = stackIndices[dy];
                    mref<element> stack (stacks+dy*stackSize, stackSize);
                    if(x==stack[i].cx) i--;
                    assert_(i>=0);
                    int sx = stack[i].x;
                    xPositionZYX[dy] = xSourceZY[dy*sX + sx];
                    yPositionZYX[dy] = ySourceZY[dy*sX + sx];
                    zPositionZYX[dy] = sx;
                }
            }
        }
    });
    positionX.squared=false, positionX.maximum=sourceX.maximum;
    positionY.squared=false, positionY.maximum=sourceY.maximum;
    positionZ.squared=false, positionZ.maximum=sX-1;
}

/// Computes position of nearest background (X pass)
class(PositionX, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint index) override { int sizes[]={4, 2}; return sizes[index]; }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        for(Volume& volume: outputs) { volume.sampleCount=rotate(volume.sampleCount); volume.margin=rotate(volume.margin); volume.origin=rotate(volume.origin); }
        perpendicularBisectorEuclideanDistanceTransform(outputs[0],outputs[1],inputs[0]);
    }
};

/// Computes position of nearest background (Y pass)
class(PositionY, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint index) override { int sizes[]={4, 2, 2}; return sizes[index]; }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        for(Volume& volume: outputs) { volume.sampleCount=rotate(volume.sampleCount); volume.margin=rotate(volume.margin); volume.origin=rotate(volume.origin); }
        perpendicularBisectorEuclideanDistanceTransform(outputs[0],outputs[1],outputs[2],inputs[0],inputs[1]);
    }
};

/// Computes position of nearest background (Y pass)
class(PositionZ, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint index) override { int sizes[]={2, 2, 2}; return sizes[index]; }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        for(Volume& volume: outputs) { volume.sampleCount=rotate(volume.sampleCount); volume.margin=rotate(volume.margin); volume.origin=rotate(volume.origin); }
        perpendicularBisectorEuclideanDistanceTransform(outputs[0],outputs[1],outputs[2],inputs[0],inputs[1],inputs[2]);
    }
};

/// Computes distance field from feature transform (for visualization)
class(Distance, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint index) override { int sizes[]={2}; return sizes[index]; }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        const Volume16& pX = inputs[0];
        const Volume16& pY = inputs[1];
        const Volume16& pZ = inputs[2];
        Volume16& target = outputs[0];
        uint maximum=0;
        for(int z: range(target.margin.z, target.sampleCount.z-target.margin.z)) {
            for(int y: range(target.margin.y, target.sampleCount.y-target.margin.y)) {
                for(int x: range(target.margin.x, target.sampleCount.x-target.margin.x)) {
                    int dx = x-pX(x,y,z), dy = y-pY(x,y,z), dz = z-pZ(x,y,z);
                    uint d = dx*dx + dy*dy + dz*dz;
                    target(x,y,z) = d;
                    maximum = max(maximum, d);
                }
            }
        }
        assert_(maximum<(1u<<(8*target.sampleSize)));
        target.maximum=maximum;
        target.squared = true;
    }
};
#endif
