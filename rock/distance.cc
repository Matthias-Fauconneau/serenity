#include "volume-operation.h"
#include "time.h"
#include "simd.h"

// FIXME: try to factor these

// X
void perpendicularBisectorEuclideanDistanceTransform(Volume32& target, Volume16& positionX, const Volume32& source) {
    setBorders(target);
    const int sX=source.sampleCount.x, sY=source.sampleCount.y, sZ=source.sampleCount.z;
    const int tX=sY, tY=sZ;
    int marginX=source.margin.x-1, marginY=source.margin.y-1, marginZ=source.margin.z-1;
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
    const int sX=source.sampleCount.x, sY=source.sampleCount.y, sZ=source.sampleCount.z;
    const int tX=sY, tY=sZ;
    int marginX=source.margin.x-1, marginY=source.margin.y-1, marginZ=source.margin.z-1;
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
void perpendicularBisectorEuclideanDistanceTransform(Volume32& target, Volume16& positionX, Volume16& positionY, Volume16& positionZ, const Volume32& source, const Volume16& sourceX, const Volume16& sourceY) {
    setBorders(target);
    const int sX=source.sampleCount.x, sY=source.sampleCount.y, sZ=source.sampleCount.z;
    const int tX=sY, tY=sZ;
    int marginX=source.margin.x-1, marginY=source.margin.y-1, marginZ=source.margin.z-1;
    const uint32* const sourceData = source;
    const uint16* const xSourceData = sourceX;
    const uint16* const ySourceData = sourceY;
    uint32* const targetData = target;
    uint16* const xPositionData = positionX;
    uint16* const yPositionData = positionY;
    uint16* const zPositionData = positionZ;
    parallel(marginZ, sZ-marginZ, [&](uint, uint z) {
        const uint32* const sourceZ = sourceData+z*sX*sY;
        const uint16* const xSourceZ = xSourceData+z*sX*sY;
        const uint16* const ySourceZ = ySourceData+z*sX*sY;
        uint32* const targetZ = targetData+z*tX;
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
            uint32* const targetZY = targetZ+y;
            uint16* const xPositionZY = xPositionZ+y;
            uint16* const yPositionZY = yPositionZ+y;
            uint16* const zPositionZY = zPositionZ+y;
            for(int x=sX-1-marginX; x>=marginX; x--) {
                uint32* const targetZYX = targetZY+x*tX*tY;
                uint16* const xPositionZYX = xPositionZY+x*tX*tY;
                uint16* const yPositionZYX = yPositionZY+x*tX*tY;
                uint16* const zPositionZYX = zPositionZY+x*tX*tY;
                for(uint dy=0; dy<unroll; dy++) {
                    int& i = stackIndices[dy];
                    mref<element> stack (stacks+dy*stackSize, stackSize);
                    if(x==stack[i].cx) i--;
                    assert_(i>=0);
                    int sx = stack[i].x;
                    int sqRadius = x * (x - 2*sx) + stack[i].sd;
                    targetZYX[dy] = sqRadius;
                    xPositionZYX[dy] = xSourceZY[dy*sX + sx];
                    yPositionZYX[dy] = ySourceZY[dy*sX + sx];
                    zPositionZYX[dy] = sx;
                }
            }
        }
    });
    target.squared=true;
    positionX.squared=false, positionX.maximum=sourceX.maximum;
    positionY.squared=false, positionY.maximum=sourceY.maximum;
    positionZ.squared=false, positionZ.maximum=sX-1;
}

/// Computes distance field to nearest background (X pass)
class(DistanceX, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint index) override { int sizes[]={4, 2}; return sizes[index]; }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        for(Volume& volume: outputs) { volume.sampleCount=rotate(volume.sampleCount); volume.margin=rotate(volume.margin); }
        perpendicularBisectorEuclideanDistanceTransform(outputs[0],outputs[1],inputs[0]);
    }
};

/// Computes distance field to nearest background (Y pass)
class(DistanceY, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint index) override { int sizes[]={4, 2, 2}; return sizes[index]; }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        for(Volume& volume: outputs) { volume.sampleCount=rotate(volume.sampleCount); volume.margin=rotate(volume.margin); }
        perpendicularBisectorEuclideanDistanceTransform(outputs[0],outputs[1],outputs[2],inputs[0],inputs[1]);
    }
};

/// Computes distance field to nearest background (Y pass)
class(DistanceZ, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint index) override { int sizes[]={4, 2, 2, 2}; return sizes[index]; }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        for(Volume& volume: outputs) { volume.sampleCount=rotate(volume.sampleCount); volume.margin=rotate(volume.margin); }
        perpendicularBisectorEuclideanDistanceTransform(outputs[0],outputs[1],outputs[2],outputs[3],inputs[0],inputs[1],inputs[2]);
        Volume& target = outputs[0];
        target.maximum=maximum((const Volume32&)target);
        if(target.maximum < (1ul<<(8*(target.sampleSize/2)))) { // Packs outputs if needed
            const Volume32& target32 = target;
            target.sampleSize = target.sampleSize / 2;
            pack(target, target32);
        }
    }
};
