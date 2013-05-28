#include "volume-operation.h"
#include "thread.h"
#include "time.h"

constexpr int tileSide = 32; // 32³ ~ 64kB ~ L1 ~ √1024 ~ 32K tiles ~ radius (might be too tight for L1 but 16³ would be too small)
struct Ball { uint16 x,y,z,sqRadius; };
struct Tile { uint ballCount=0; uint8 pad[sizeof(Ball)-sizeof(uint)]; Ball balls[16384-1]; }; //128KB / tile ~ 4GiB for 32³ tiles = 1024³ voxels

/// Bins each skeleton voxel as a ball to 32³ tiles
void bin(Volume& target, const Volume16& source) {
    const uint16* const sourceData = source;
    const int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY = X*Y;
    assert(X%tileSide==0 && Y%tileSide==0 && Z%tileSide==0);
    int marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    assert(offsetX && offsetY && offsetZ);

    Tile* const targetData = (Tile*)target.data.data;
    for(uint i: range(X/tileSide*Y/tileSide*Z/tileSide)) targetData[i].ballCount=0;

    //parallel(marginZ,Z-marginZ, [&](uint id, uint z) { //TODO: lockless list, Z-order
    for(int z: range(marginZ, Z-marginZ)) {
        for(int y=marginY; y<Y-marginY; y++) {
            for(int x=marginX; x<X-marginX; x++) {
                uint sqRadius = sourceData[offsetZ[z]+offsetY[y]+offsetX[x]];
                if(!sqRadius) continue;
                float ballRadius = sqrt(sqRadius);
                int radius = ceil(ballRadius);
                for(int dz=(z-radius)/tileSide; dz<=(z+radius)/tileSide; dz++) {
                    Tile* const targetZ= targetData + dz * (XY/tileSide/tileSide);
                    int tileZ = dz*tileSide;
                    for(int dy=(y-radius)/tileSide; dy<=(y+radius)/tileSide; dy++) {
                        Tile* const targetZY= targetZ + dy * (X/tileSide);
                        int tileY = dy*tileSide;
                        for(int dx=(x-radius)/tileSide; dx<=(x+radius)/tileSide; dx++) {
                            Tile* const tile = targetZY + dx;
                            int tileX = dx*tileSide;
                            int vx = x-tileX, vy=y-tileY, vz=z-tileZ; // Vector from (0,0,0) corner to ball center
                            uint dmin = 0; // Tests box-ball intersection
                            if(vx < 0) dmin += sqr(vx); else if(vx-(tileSide-1) > 0) dmin += sqr(vx-(tileSide-1));
                            if(vy < 0) dmin += sqr(vy); else if(vy-(tileSide-1) > 0) dmin += sqr(vy-(tileSide-1));
                            if(vz < 0) dmin += sqr(vz); else if(vz-(tileSide-1) > 0) dmin += sqr(vz-(tileSide-1));
                            if(dmin < sqRadius) {
                                float r = norm(vec3(tileX,tileY,tileZ)+vec3((tileSide-1)/2.)-vec3(x,y,z)), tileRadius = sqrt(3.*sqr((tileSide-1)/2.));
                                assert_(r<tileRadius+ballRadius); // Intersects ball with the tile bounding sphere
                                assert_(tile->ballCount<sizeof(tile->balls)/sizeof(Ball), tile->ballCount);
                                tile->balls[tile->ballCount++] = {uint16(x),uint16(y),uint16(z),uint16(sqRadius)}; // Appends the ball to intersecting tiles (TODO: lockless list)
                            }
                        }
                    }
                }
            }
        }
    } //);
    uint maximum=0;
    for(uint z: range(0, Z/tileSide)) {
        const Tile* const sourceZ= targetData + z * (XY/tileSide/tileSide);
        for(int y=0; y<Y/tileSide; y++) {
            const Tile* const sourceZY= sourceZ + y * (X/tileSide);
            for(int x=0; x<X/tileSide; x++) {
                const Tile* const tile = sourceZY + x;
                maximum = max(maximum, tile->ballCount);
            }
        }
    }
    log(maximum);
}
PASS(Bin, uint8[sizeof(Tile)/tileSide/tileSide/tileSide], bin);

/// Rasterizes each skeleton voxel as a ball (with maximum blending)
void rasterize(Volume16& target, const Volume& source) {
    const Tile* const sourceData = (Tile*)source.data.data;
    const int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY = X*Y;

    uint16* const targetData = target;
    interleavedLookup(target);
    const uint* const offsetX = target.offsetX, *offsetY = target.offsetY, *offsetZ = target.offsetZ;
    Time time; Time report;
    parallel(0, Z/tileSide, [&](uint id, uint z) { //FIXME: Z-order
    //for(uint z: range(0, Z/tileSide)) { uint id=0;
        if(id==0 && report/1000>=4) log(z,"/", Z/tileSide, (z*tileSide*XY/1024./1024.)/(time/1000.), "MS/s"), report.reset();
        const Tile* const sourceZ= sourceData + z * (XY/tileSide/tileSide);
        for(int y=0; y<Y/tileSide; y++) {
            const Tile* const sourceZY= sourceZ + y * (X/tileSide);
            for(int x=0; x<X/tileSide; x++) {
                const Tile* const tile = sourceZY + x;
                int tx = x*tileSide, ty = y*tileSide, tz = z*tileSide;
                int oz=offsetZ[tz], oy=offsetY[ty], ox=offsetX[tx];
                clear(targetData+oz+oy+ox, tileSide*tileSide*tileSide);
                for(uint i=0; i<tile->ballCount; i++) { // Rasterizes each ball intersecting this tile
                    const Ball& ball = tile->balls[i];
                    int cx=ball.x-tx, cy=ball.y-ty, cz=ball.z-tz, sqRadius=ball.sqRadius;
                    //uint pass=0;
                    for(int dz=0; dz<tileSide; dz++) {
                        uint16* const targetZ= targetData + oz + offsetZ[dz];
                        for(int dy=0; dy<tileSide; dy++) {
                            uint16* const targetZY= targetZ + oy + offsetY[dy];
                            for(int dx=0; dx<tileSide; dx++) {
                                uint16* const voxel =  targetZY + ox + offsetX[dx];
                                if(sqRadius>voxel[0] && sqr(cx-dx)+sqr(cy-dy)+sqr(cz-dz)<sqRadius) voxel[0] = sqRadius;
                                //if(sqr(cx-dx)+sqr(cy-dy)+sqr(cz-dz)<sqRadius) { if(sqRadius>voxel[0]) voxel[0] = sqRadius; pass++; }
                            }
                        }
                    }
                    //assert_(pass, x*tileSide,y*tileSide,z*tileSide, ball.x, ball.y, ball.z);
                }
            }
        }
    } );
    target.squared = true;
    assert_(target.maximum == source.maximum);
}
PASS(Rasterize, uint16, rasterize);
