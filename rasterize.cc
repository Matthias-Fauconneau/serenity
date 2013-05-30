#include "volume-operation.h"
#include "thread.h"
#include "time.h"

constexpr int tileSide = 16, tileSize=tileSide*tileSide*tileSide; //~ most frequent radius -> 16³ = 4³ blocks of 4³ voxels = 8kB. Fits L1 but many tiles (1024³ = 256K tiles = 16GiB (virtual))
const int blockSide = 4, blockSize=blockSide*blockSide*blockSide, blockCount=tileSide/blockSide; //~ coherency size -> Skips processing 4³ voxel whenever possible
struct Ball { uint16 x,y,z,sqRadius; };
struct Tile { uint64 ballCount=0; Ball balls[tileSize*2-1]; }; // 16³ tiles -> 64KB/tile ~ 16GiB (virtual) for 1K³

/// Tests box-ball intersection
inline uint dmin(int size, int vx, int vy, int vz) {
    uint dmin = 0;
    if(vx < 0) dmin += sqr(vx); else if(vx-(size-1) > 0) dmin += sqr(vx-(size-1));
    if(vy < 0) dmin += sqr(vy); else if(vy-(size-1) > 0) dmin += sqr(vy-(size-1));
    if(vz < 0) dmin += sqr(vz); else if(vz-(size-1) > 0) dmin += sqr(vz-(size-1));
    return dmin;
}

/// Bins each skeleton voxel as a ball to tiles of 16³ voxels
void bin(Volume& target, const Volume16& source) {
    const uint16* const sourceData = source;
    const int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    assert_(X%tileSide==0 && Y%tileSide==0 && Z%tileSide==0);
    int marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    assert(offsetX && offsetY && offsetZ);

    Tile* const targetData = reinterpret_cast<Tile*>(target.data.data);
    assert_(uint(X/tileSide*Y/tileSide*Z/tileSide) == target.size()*target.sampleSize/sizeof(Tile));
    for(uint i: range(X/tileSide*Y/tileSide*Z/tileSide)) targetData[i].ballCount=0;

    parallel(marginZ,Z-marginZ, [&](uint, uint z) { //TODO: Z-order
        for(int y=marginY; y<Y-marginY; y++) {
            for(int x=marginX; x<X-marginX; x++) {
                uint sqRadius = sourceData[offsetZ[z]+offsetY[y]+offsetX[x]];
                if(!sqRadius) continue;
                float ballRadius = sqrt(sqRadius);
                int radius = ceil(ballRadius);
                for(int dz=(int(z)-radius)/tileSide; dz<=(int(z)+radius)/tileSide; dz++) {
                    for(int dy=(y-radius)/tileSide; dy<=(y+radius)/tileSide; dy++) {
                        for(int dx=(x-radius)/tileSide; dx<=(x+radius)/tileSide; dx++) {
                            Tile* const tile = targetData + dz * (X/tileSide*Y/tileSide) + dy * (X/tileSide) + dx;
                            int tileX = dx*tileSide, tileY = dy*tileSide, tileZ = dz*tileSide;
                            if(dmin(tileSide,x-tileX,y-tileY,int(z)-tileZ) < sqRadius) { // Intersects tile
                                float r = norm(vec3(tileX,tileY,tileZ)+vec3((tileSide-1)/2.)-vec3(x,y,z)), tileRadius = sqrt(3.*sqr((tileSide-1)/2.));
                                assert_(r<tileRadius+ballRadius); // Intersects ball with the tile bounding sphere
                                assert_(tile->ballCount<sizeof(tile->balls)/sizeof(Ball), tile->ballCount, dmin(tileSide,x-tileX,y-tileY,z-tileZ), dx,dy,dz, sqRadius,ballRadius,  x-tileX,y-tileY,int(z)-tileZ, norm(vec3(0,y-tileY,int(z)-tileZ)), x,y,z, x+ballRadius,y+ballRadius,z+ballRadius);
                                uint index = __sync_fetch_and_add(&tile->ballCount,1); // Thread-safe lock-free add
                                tile->balls[index] = {uint16(x),uint16(y),uint16(z),uint16(sqRadius)}; // Appends the ball to intersecting tiles
                            }
                        }
                    }
                }
            }
        }
    } );
    //uint64 maximum=0; for(uint i: range(X/tileSide*Y/tileSide*Z/tileSide)) maximum = max(maximum, targetData[i].ballCount); log(maximum);
}
PASS(Bin, uint8[sizeof(Tile)/tileSide/tileSide/tileSide], bin);

/// Rasterizes each skeleton voxel as a ball (with maximum blending)
void rasterize(Volume16& target, const Volume& source) {
    const Tile* const sourceData = (Tile*)source.data.data;
    const int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    uint tileCount = X/tileSide*Y/tileSide*Z/tileSide;

    uint16* const targetData = target;
    interleavedLookup(target);
    const uint* const offsetX = target.offsetX, *offsetY = target.offsetY, *offsetZ = target.offsetZ;

    Time time; Time report;
    parallel(tileCount, [&](uint id, uint i) {
        if(id==0 && report/1000>=7) log(i,"/", tileCount, (i*tileSize/1024./1024.)/(time/1000.), "MS/s"), report.reset();
        int z = (i/(X/tileSide*Y/tileSide))%(Z/tileSide), y=(i/(X/tileSide))%(Y/tileSide), x=i%(X/tileSide); // Extracts tile coordinates back from index
        int tileX = x*tileSide, tileY = y*tileSide, tileZ = z*tileSide; // first voxel coordinates
        const Tile& balls = sourceData[i]; // Tile primitives, i.e. balls list [seq]
        struct Block { uint16 min=0, max=0; } blocks[blockCount*blockCount*blockCount]; // min/max values for each block (for hierarchical culling) (ala Hi-Z) [256B]
        uint16 tile[tileSize] = {}; // Half-tiled target buffer (using 4³ bricks instead of 2³ (Z-curve)) to access without lookup tables [8K]
        for(uint i=0; i<balls.ballCount; i++) { // Rasterizes each ball intersecting this tile
            const Ball& ball = balls.balls[i];
            int tileBallX=ball.x-tileX, tileBallY=ball.y-tileY, tileBallZ=ball.z-tileZ, sqRadius=ball.sqRadius;
#if 0 // Full 60s
            for(int dz=0; dz<tileSide; dz++) for(int dy=0; dy<tileSide; dy++) for(int dx=0; dx<tileSide; dx++) {
                uint16* const voxel = tile + offsetX[dx] + offsetY[dy] + offsetZ[dz];
                if(sqRadius>voxel[0] && sqr(tileBallX-dx)+sqr(tileBallY-dy)+sqr(tileBallZ-dz)<sqRadius) voxel[0] = sqRadius;
            }
#elif 0 // Clip 18s
            int radius = ceil(sqrt(sqRadius));
            for(int dz=max(0,cz-radius); dz<min(tileSide,cz+radius); dz++)
                for(int dy=max(0,cy-radius); dy<min(tileSide,cy+radius); dy++)
                    for(int dx=max(0,cx-radius); dx<min(tileSide,cx+radius); dx++) {
                        uint16* const voxel = target + offsetX[dx] + offsetY[dy] + offsetZ[dz];
                        if(sqRadius>voxel[0] && sqr(tileBallX-dx)+sqr(tileBallY-dy)+sqr(tileBallZ-dz)<sqRadius) voxel[0] = sqRadius;
                    }
#else // Recursive 15s (TODO: SIMD)
            for(int dz=0; dz<blockCount; dz++) for(int dy=0; dy<blockCount; dy++) for(int dx=0; dx<blockCount; dx++) {
                Block& HiZ = blocks[dz*blockCount*blockCount+dy*blockCount+dx];
                if(sqRadius <= HiZ.min) continue; //Skips whole block if all voxels are already larger
                int blockX = dx*blockSide, blockY = dy*blockSide, blockZ = dz*blockSide;
                int blockBallX=tileBallX-blockX, blockBallY=tileBallY-blockY, blockBallZ=tileBallZ-blockZ;
                int dmin = ::dmin(blockSide, blockBallX, blockBallY, blockBallZ);
                if(dmin >= sqRadius) continue; // Rejects whole block
                uint16* const block = tile + (dz*blockCount*blockCount + dy*blockCount + dx)*blockSize;
                const int blockSqRadius = 3*(blockSide-1)*(blockSide-1);
                if(sqRadius>=HiZ.max) {
                    if(dmin == 0 && sqRadius>blockSqRadius) { // Accepts whole block
                        for(uint i=0; i<blockSize; i++) block[i]=sqRadius;
                        HiZ.min = sqRadius, HiZ.max = sqRadius;
                    }
                    HiZ.max = sqRadius;
                }
                uint min=-1;
                for(int dz=0; dz<blockSide; dz++) for(int dy=0; dy<blockSide; dy++) for(int dx=0; dx<blockSide; dx++) {
                    uint16* const voxel = block + dz*blockSide*blockSide + dy*blockSide + dx;
                    if(sqRadius>voxel[0] && sqr(blockBallX-dx)+sqr(blockBallY-dy)+sqr(blockBallZ-dz)<sqRadius) voxel[0] = sqRadius;
                    if(voxel[0]<min) min = voxel[0]; // FIXME: skip when possible
                }
                HiZ.min=min;
            }
#endif
        }
        // Writes out fully interleaved target for compatibility (visualization methods currently only accepts untiled or fully tiled (Z-order) buffers)
        uint16* const target = targetData + offsetX[tileX] + offsetY[tileY] + offsetZ[tileZ];
        for(int dz=0; dz<blockCount; dz++) for(int dy=0; dy<blockCount; dy++) for(int dx=0; dx<blockCount; dx++) {
            int blockX = dx*blockSide, blockY = dy*blockSide, blockZ = dz*blockSide;
            const uint16* const block = tile + (dz*blockCount*blockCount + dy*blockCount + dx)*blockSize;
            uint16* const targetBlock = target + offsetX[blockX] + offsetY[blockY] + offsetZ[blockZ];
            for(int dz=0; dz<blockSide; dz++) for(int dy=0; dy<blockSide; dy++) for(int dx=0; dx<blockSide; dx++) {
                targetBlock[offsetX[dx] + offsetY[dy] + offsetZ[dz]] = block[dz*blockSide*blockSide + dy*blockSide + dx];
            }
        }
    } );
    target.squared = true;
    assert_(target.maximum == source.maximum);
}
PASS(Rasterize, uint16, rasterize);
