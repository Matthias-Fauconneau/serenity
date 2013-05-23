#include "volume-operation.h"
#include "sample.h"
#include "thread.h"
#include "time.h"

/// Rasterizes each distance field voxel as a ball (with maximum blending)
void rasterize(Volume16& target, const Volume16& source) {
    const uint16* const sourceData = source;
    uint16* const targetData = target;
    const int X=target.x, Y=target.y, Z=target.z, XY = X*Y;
    int marginX=target.marginX, marginY=target.marginY, marginZ=target.marginZ;
    clear(targetData, X*Y*Z);
    interleavedLookup(target);
    const uint* const offsetX = target.offsetX;
    const uint* const offsetY = target.offsetY;
    const uint* const offsetZ = target.offsetZ;
    assert_(offsetX && offsetY && offsetZ);
    Time time; Time report;
    parallel(marginZ,Z-marginZ, [&](uint id, uint z) { //FIXME: Z-order
        if(id==0 && report/1000>=4) log(z-marginZ,"/", Z-2*marginZ, (z*XY/1024./1024.)/(time/1000.), "MS/s"), report.reset();
        for(int y=marginY; y<Y-marginY; y++) {
            for(int x=marginX; x<X-marginX; x++) {
                int sqRadius = sourceData[offsetZ[z]+offsetY[y]+offsetX[x]];
                if(!sqRadius) continue;
                int radius = ceil(sqrt(sqRadius));
                assert(radius<=x && radius<=y && radius<=int(z) && radius<X-1-x && radius<Y-1-y && radius<Z-1-int(z));
                for(int dz=-radius; dz<=radius; dz++) {
                    uint16* const targetZ= targetData + offsetZ[z+dz];
                    for(int dy=-radius; dy<=radius; dy++) {
                        uint16* const targetZY= targetZ + offsetY[y+dy];
                        for(int dx=-radius; dx<=radius; dx++) {
                            uint16* const targetZYX= targetZY + offsetX[x+dx];
                            if(dx*dx+dy*dy+dz*dz<sqRadius) { // Rasterizes ball
                                //uint unused r = sourceData[offsetZ[z+dz]+offsetY[y+dy]+offsetX[x+dx]]; assert_(r, r, x,y,z, dx,dy,dz, dx*dx+dy*dy+dz*dz,sqRadius);
                                while(sqRadius > (int)(targetZYX[0]) && !__sync_bool_compare_and_swap(targetZYX, targetZYX[0], sqRadius)); // Stores maximum radius (thread-safe)
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

/// Rasterizes each distance field voxel as a ball (with maximum blending)
class(Rasterize, Operation), virtual VolumePass<uint16> {
    void execute(const map<ref<byte>, Variant>& args, Volume16& target, const Volume& source) override {
        rasterize(target, source);
        // Computes histogram of maximal ball radii
        ref<byte> name = args.at("name"_);
        Folder resultFolder (args.at("resultFolder"_));
        Time time;
        Sample squaredMaximum = histogram(target, args.contains("cylinder"_));
        squaredMaximum[0] = 0; // Clears background voxel count to plot with a bigger Y scale
        float scale = toDecimal(args.value("resolution"_,"1"_));
        writeFile(name+".maximum.tsv"_, toASCII(squaredMaximum, false, true, scale), resultFolder);
        log("√histogram", name, time);
    }
};
