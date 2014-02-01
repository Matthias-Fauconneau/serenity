#include "volume-operation.h"
#include "thread.h"
#include "time.h"

/// Tiles a volume recursively into bricks (using 3D Z ordering)
generic void zOrder(VolumeT<T>& target, const VolumeT<T>& source) {
    assert_(!source.tiled());
    const uint64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    const T* const sourceData = source;
    T* const targetData = target;
    interleavedLookup(target);
    const uint64* const offsetX = target.offsetX, *offsetY = target.offsetY, *offsetZ = target.offsetZ;
    for(uint z=0; z<Z; z++) for(uint y=0; y<Y; y++) for(uint x=0; x<X; x++) targetData[offsetZ[z]+offsetY[y]+offsetX[x]] = sourceData[z*X*Y + y*X + x];
}
class(ZOrder, Operation), virtual VolumeOperation {
    uint outputSampleSize(const Dict&, const ref<const Result*>& inputs, uint) override { return toVolume(*inputs[0]).sampleSize; }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        assert_(outputs);
        /***/ if(inputs[0].sampleSize==1) zOrder<uint8>(outputs[0],inputs[0]);
        else if(inputs[0].sampleSize==2) zOrder<uint16>(outputs[0],inputs[0]);
        else error(inputs[0].sampleSize);
    }
};

constexpr int tileSide = 16, tileSize=tileSide*tileSide*tileSide; //~ most frequent radius -> 16³ = 4³ blocks of 4³ voxels = 8kB. Fits L1 but many tiles (1024³ = 256K tiles)
const int blockSide = 4, blockSize=blockSide*blockSide*blockSide, blockCount=tileSide/blockSide; //~ coherency size -> Skips processing 4³ voxel whenever possible
struct Ball { uint16 x,y,z,sqRadius, attribute; };
const int overcommit = 16; // Allows more spheres intersections than voxels inside a tile (only virtual memory is reserved and committed only as needed when filling tile's sphere lists)
struct Tile { uint64 ballCount=0; Ball balls[tileSize*overcommit-1]; }; // 16³ tiles -> 64KB/tile ~ 16GiB (virtual) for 1K³

/// Tests box-ball intersection
inline uint dmin(int size, int vx, int vy, int vz) {
    uint dmin = 0;
    if(vx < 0) dmin += sq(vx); else if(vx-(size-1) > 0) dmin += sq(vx-(size-1));
    if(vy < 0) dmin += sq(vy); else if(vy-(size-1) > 0) dmin += sq(vy-(size-1));
    if(vz < 0) dmin += sq(vz); else if(vz-(size-1) > 0) dmin += sq(vz-(size-1));
    return dmin;
}

/// Bins each skeleton voxel as a ball to tiles of 16³ voxels
void bin(Volume& target, const Volume16& source, const Volume16& attribute) {
    const uint16* const sourceData = source;
    const uint16* const attributeData = attribute;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    assert_(X%tileSide==0 && Y%tileSide==0 && Z%tileSide==0);
    const int marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    assert_(source.tiled());
    const ref<uint64> offsetX = source.offsetX, offsetY = source.offsetY, offsetZ = source.offsetZ;

    Tile* const targetData = reinterpret_cast<Tile*>(target.data.begin());
    assert_(uint(X/tileSide*Y/tileSide*Z/tileSide) == target.size()*target.sampleSize/sizeof(Tile));
    for(uint i: range(X/tileSide*Y/tileSide*Z/tileSide)) targetData[i].ballCount=0;

    parallel(marginZ,Z-marginZ, [&](uint, uint z) { //TODO: Z-order
        for(int y=marginY; y<Y-marginY; y++) {
            for(int x=marginX; x<X-marginX; x++) {
                uint64 offset = offsetZ[z]+offsetY[y]+offsetX[x];
                uint16 sqRadius = sourceData[offset];
                uint16 attribute = attributeData[offset];
                if(!sqRadius) continue;
                float ballRadius = sqrt(float(sqRadius));
                int radius = ceil(ballRadius);
                for(int dz=max(0,(int(z)-radius))/tileSide; dz<=min<int>(Z-1,(int(z)+radius))/tileSide; dz++) {
                    for(int dy=max(0,(y-radius))/tileSide; dy<=min<int>(Y-1,(y+radius))/tileSide; dy++) {
                        for(int dx=max(0,(x-radius))/tileSide; dx<=min<int>(X-1,(x+radius))/tileSide; dx++) {
                            Tile* const tile = targetData + dz * (X/tileSide*Y/tileSide) + dy * (X/tileSide) + dx;
                            int tileX = dx*tileSide, tileY = dy*tileSide, tileZ = dz*tileSide;
                            if(dmin(tileSide,x-tileX,y-tileY,int(z)-tileZ) < sqRadius) { // Intersects tile
                                /*float r = norm(vec3(tileX,tileY,tileZ)+vec3((tileSide-1)/2.)-vec3(x,y,z)), tileRadius = sqrt(3.*sq((tileSide-1)/2.));
                                assert(r<tileRadius+ballRadius); // Intersects ball with the tile bounding sphere*/
                                assert_(tile->ballCount<sizeof(tile->balls)/sizeof(Ball), dx, X/tileSide, dy, Y/tileSide, dz, Z/tileSide,  tile->ballCount);
                                uint index = __sync_fetch_and_add(&tile->ballCount,1); // Thread-safe lock-free add
                                tile->balls[index] = {uint16(x),uint16(y),uint16(z),sqRadius,attribute}; // Appends the ball to intersecting tiles
                            }
                        }
                    }
                }
            }
        }
    } );
#if DEBUG
    uint64 maximum=0; for(uint i: range(X/tileSide*Y/tileSide*Z/tileSide)) maximum = max(maximum, targetData[i].ballCount); log(maximum); assert(maximum);
#endif
    target.maximum=attribute.maximum;
}
class(Bin, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(Tile)/tileSide/tileSide/tileSide; }
    virtual void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override { bin(outputs[0], inputs[0], inputs[1]); }
};

/// Rasterizes each skeleton voxel as a ball (with maximum blending)
void rasterize(Volume16& target, const Volume& source) {
    const Tile* const sourceData = (Tile*)source.data.data;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    uint tileCount = X/tileSide*Y/tileSide*Z/tileSide;

    uint16* const targetData = target;
    assert_(target.tiled());
    const ref<uint64> offsetX = target.offsetX, offsetY = target.offsetY, offsetZ = target.offsetZ;

    Time time; Time report;
    parallel(tileCount, [&](uint id, uint i) {
        if(id==0 && report/1000>=7) log(i,"/", tileCount, (i*tileSize/1024./1024.)/(time/1000.), "MS/s"), report.reset();
        int z = (i/(X/tileSide*Y/tileSide))%(Z/tileSide), y=(i/(X/tileSide))%(Y/tileSide), x=i%(X/tileSide); // Extracts tile coordinates back from index
        int tileX = x*tileSide, tileY = y*tileSide, tileZ = z*tileSide; // first voxel coordinates
        const Tile& balls = sourceData[i]; // Tile primitives, i.e. balls list [seq]
        struct Block { uint16 min=0, max=0; } blocks[blockCount*blockCount*blockCount]; // min/max values for each block (for hierarchical culling) (ala Hi-Z) [256B]
        uint16 tileR[tileSize] = {}; // Half-tiled 'R'-buffer (using 4³ bricks instead of 2³ (Z-curve)) to access without lookup tables [8K]
        for(uint i=0; i<balls.ballCount; i++) { // Rasterizes each ball intersecting this tile
            const Ball& ball = balls.balls[i];
            int tileBallX=ball.x-tileX, tileBallY=ball.y-tileY, tileBallZ=ball.z-tileZ, sqRadius=ball.sqRadius;
            // Recursive 12s (TODO: SIMD)
            for(int dz=0; dz<blockCount; dz++) for(int dy=0; dy<blockCount; dy++) for(int dx=0; dx<blockCount; dx++) {
                Block& HiZ = blocks[dz*blockCount*blockCount+dy*blockCount+dx];
                if(sqRadius <= HiZ.min) continue; // Rejects whole block if all voxels are already larger
                int blockX = dx*blockSide, blockY = dy*blockSide, blockZ = dz*blockSide;
                int blockBallX=tileBallX-blockX, blockBallY=tileBallY-blockY, blockBallZ=tileBallZ-blockZ;
                int dmin = ::dmin(blockSide, blockBallX, blockBallY, blockBallZ);
                if(dmin >= sqRadius)  continue; // Rejects whole block if fully outside
                uint16* const blockR = tileR + (dz*blockCount*blockCount + dy*blockCount + dx)*blockSize;
                const int blockSqRadius = 3*(blockSide-1)*(blockSide-1);
                if(sqRadius>=HiZ.max) {
                    if(dmin == 0 && sqRadius>blockSqRadius) { // Accepts whole block
                        for(uint i=0; i<blockSize; i++) {
                            blockR[i]=sqRadius;
                        }
                        HiZ.min = sqRadius, HiZ.max = sqRadius;
                        continue;
                    }
                    HiZ.max = sqRadius;
                }
                uint min=-1;
                for(int dz=0; dz<blockSide; dz++) for(int dy=0; dy<blockSide; dy++) for(int dx=0; dx<blockSide; dx++) {
                    uint16& R = blockR[dz*blockSide*blockSide + dy*blockSide + dx];
                    if(sqRadius>R && sq(blockBallX-dx)+sq(blockBallY-dy)+sq(blockBallZ-dz)<sqRadius) R = sqRadius;
                    if(R<min) min = R; // FIXME: skip when possible
                }
                HiZ.min=min;
            }
        }
        // Writes out fully interleaved target for compatibility (metadata tiled flags currently only define untiled or fully tiled (Z-order) volumes)
        uint16* const targetTile = targetData + offsetX[tileX] + offsetY[tileY] + offsetZ[tileZ];
        for(int dz=0; dz<blockCount; dz++) for(int dy=0; dy<blockCount; dy++) for(int dx=0; dx<blockCount; dx++) {
            int blockX = dx*blockSide, blockY = dy*blockSide, blockZ = dz*blockSide;
            const uint16* const block = tileR + (dz*blockCount*blockCount + dy*blockCount + dx)*blockSize;
            uint16* const targetBlock = targetTile + offsetX[blockX] + offsetY[blockY] + offsetZ[blockZ];
            for(int dz=0; dz<blockSide; dz++) for(int dy=0; dy<blockSide; dy++) for(int dx=0; dx<blockSide; dx++) {
                targetBlock[offsetX[dx] + offsetY[dy] + offsetZ[dz]] = block[dz*blockSide*blockSide + dy*blockSide + dx];
            }
        }
    } );
    target.squared = true;
    assert_(target.maximum == source.maximum);
}
defineVolumePass(Rasterize, uint16, rasterize);

/// Rasterizes each skeleton voxel as a ball (with maximum blending and maximum attribute override)
void rasterizeAttribute(Volume16& target, const Volume& source) {
    const Tile* const sourceData = (Tile*)source.data.data;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    uint tileCount = X/tileSide*Y/tileSide*Z/tileSide;

    const mref<uint16> targetData = target;
    assert_(target.tiled());
    const ref<uint64> offsetX = target.offsetX, offsetY = target.offsetY, offsetZ = target.offsetZ;

    Time time; Time report;
    parallel(tileCount, [&](uint id, uint i) {
        if(id==0 && report/1000>=7) log(i,"/", tileCount, (i*tileSize/1024./1024.)/(time/1000.), "MS/s"), report.reset();
        int z = (i/(X/tileSide*Y/tileSide))%(Z/tileSide), y=(i/(X/tileSide))%(Y/tileSide), x=i%(X/tileSide); // Extracts tile coordinates back from index
        int tileX = x*tileSide, tileY = y*tileSide, tileZ = z*tileSide; // first voxel coordinates
        const Tile& balls = sourceData[i]; // Tile primitives, i.e. balls list [seq]
        uint16 tileR[tileSize] = {}; // Untiled 'R'-buffer
        int3 tileP[tileSize] = {}; // Untiled 'P'-buffer
        mref<int3>(tileP).clear(0);
        const mref<uint16> targetTile = targetData.slice(offsetX[tileX] + offsetY[tileY] + offsetZ[tileZ]);
        targetTile.clear();
        for(uint i=0; i<balls.ballCount; i++) { // Rasterizes each ball intersecting this tile
            const Ball& ball = balls.balls[i];
            int tileBallX=ball.x-tileX, tileBallY=ball.y-tileY, tileBallZ=ball.z-tileZ, sqRadius=ball.sqRadius;
            int3 tileBall = int3(tileBallX, tileBallY, tileBallZ);
            uint attribute=ball.attribute;
            // Clip 18s
            int radius = ceil(sqrt((real)sqRadius));
            for(int dz=max(0,tileBallZ-radius); dz<min(tileSide,tileBallZ+radius); dz++) {
                for(int dy=max(0,tileBallY-radius); dy<min(tileSide,tileBallY+radius); dy++) {
                    for(int dx=max(0,tileBallX-radius); dx<min(tileSide,tileBallX+radius); dx++) {
                        if(sq(tileBallX-dx)+sq(tileBallY-dy)+sq(tileBallZ-dz)<sqRadius) {
                            uint tileIndex = dz*tileSide*tileSide + dy*tileSide + dx;
                            uint16& R = tileR[tileIndex];
                            uint16& A =  targetTile[offsetZ[dz] + offsetY[dy] + offsetX[dx]]; // Might be faster to use an untiled buffer here
                            int3 voxel = int3(dx,dy,dz);
                            int3& p = tileP[tileIndex];
                            if(attribute==target.maximum) {
                                if(!p || sq(voxel-tileBall)<sq(voxel-p)) { // Records closest throat
                                    R = sqRadius;
                                    A = attribute;
                                    p = tileBall;
                                }
                            }
                            else if(sqRadius > R) {
#if 0
                                if(p) { // Resolve pore throats collision by taking relative nearest (flat section border)
                                    //uint16 throatR = R;
                                    uint throatD = sq(voxel-p);
                                    //uint16 poreR = sqRadius;
                                    uint poreD = sq(voxel-tileBall);
                                    if(poreD < throatD) { // Pore is nearest
                                        R = sqRadius;
                                        A = attribute;
                                    } //else throat is nearest
                                } else
#endif
                                { // Normal maximum output
                                    R = sqRadius;
                                    A = attribute;
                                }
                            }
                        }
                    }
                }
            }
        }
    } );
    target.squared = true;
    assert_(target.maximum == source.maximum);
}
defineVolumePass(RasterizeAttribute, uint16, rasterizeAttribute);
