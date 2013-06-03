#include "volume-operation.h"
#include "tiff.h"
#include "time.h"

/// Concatenates image slice files in a volume
class(Source, Operation), virtual VolumeOperation {
    uint minX, minY, minZ, maxX, maxY, maxZ;

    ref<byte> parameters() const override { static auto p="source cylinder cube"_; return p; }
    uint outputSampleSize(uint) override { return 2; }
    uint64 outputSize(const Dict& args, const ref<Result*>&, uint) override {
        Folder folder = args.at("source"_);
        array<string> slices = folder.list(Files);
        assert_(slices, args.at("source"_));
        Map file (slices.first(), folder);
        const Tiff16 image (file);
        minX=0, minY=0, minZ=0, maxX = image.width, maxY = image.height, maxZ = slices.size;
        if(args.contains("cylinder"_)) {
            if(args.at("cylinder"_)!=""_) {
                auto coordinates = apply<int64>(split(args.at("cylinder"_),','), toInteger, 10);
                int x=coordinates[0], y=coordinates[1], r=coordinates[2]; minZ=coordinates[3], maxZ=coordinates[4];
                minX=x-r, minY=y-r, maxX=x+r, maxY=y+r;
            }
            assert_(maxX-minX == maxY-minY, minX, minY, maxX, maxY);
        }
        if(args.contains("cube"_)) {
            auto coordinates = apply<int64>(split(args.at("cube"_),','), toInteger, 10);
            minX=coordinates[0], minY=coordinates[1], minZ=coordinates[2], maxX=coordinates[3], maxY=coordinates[4], maxZ=coordinates[5];
        }
        assert_(minX<maxX && minY<maxY && minZ<maxZ && maxX<=image.width && maxY<=image.height && maxZ<=slices.size, minX,minY,minZ, maxX,maxY,maxZ, image.width, image.height, slices.size);
        assert( (maxX-minX)%2 == 0 && (maxY-minY)%2 == 0 && (maxZ-minZ)%2 == 0 ); // Margins are currently always symmetric
        return nextPowerOfTwo(maxX-minX)*nextPowerOfTwo(maxY-minY)*nextPowerOfTwo(maxZ-minZ)*outputSampleSize(0);
    }

    void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>&) {
        Folder folder = args.at("source"_);
        array<string> slices = folder.list(Files);

        Volume16& target = outputs.first();
        int3 size = int3(maxX-minX, maxY-minY, maxZ-minZ);
        target.sampleCount = int3(nextPowerOfTwo(size.x), nextPowerOfTwo(size.y), nextPowerOfTwo(size.z));
        target.margin = (target.sampleCount - size)/2;
        uint X = target.sampleCount.x, Y = target.sampleCount.y, Z = target.sampleCount.z;
        uint marginX = target.margin.x, marginY = target.margin.y, marginZ = target.margin.z;
        Time time; Time report;
        uint16* const targetData = (Volume16&)outputs.first();
        for(uint z: range(size.z)) {
            if(report/1000>=7) { log(z,"/",Z, (z*X*Y*2/1024/1024)/(time/1000), "MB/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
            Tiff16(Map(slices[minZ+z],folder)).read(targetData+(marginZ+z)*X*Y + marginY*X + marginX, minX, minY, maxX-minX, maxY-minY, Y); // Directly decodes slice images into the volume
        }
        //target.maximum = (1<<(target.sampleSize*8))-1; assert(maximum(target) == target.maximum, maximum(target));
        target.maximum = maximum(target); // Some sources don't use the full range
    }
};
