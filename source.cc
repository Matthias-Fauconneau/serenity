#include "volume-operation.h"
#include "tiff.h"
#include "time.h"

/// Concatenates image slice files in a volume
class(Source, Operation), virtual VolumeOperation {
    uint minX, minY, minZ, maxX, maxY, maxZ;

    ref<ref<byte>> parameters() const override { static auto p = {"cylinder"_,"cube"_}; return p; }
    uint outputSampleSize(uint) override { return 2; }
    uint64 outputSize(const map<ref<byte>, Variant>& args, const ref<shared<Result>>&, uint) override {
        Folder folder = args.at("source"_);
        array<string> slices = folder.list(Files);
        assert(slices, args.at("source"_));
        Map file (slices.first(), folder);
        const Tiff16 image (file);
        minX=0, minY=0, minZ=0, maxX = image.width, maxY = image.height, maxZ = slices.size;
        if(args.contains("cylinder"_) && args.at("cylinder"_)!=""_) {
            auto coordinates = toIntegers(args.at("cylinder"_));
            int x=coordinates[0], y=coordinates[1], r=coordinates[2]; minZ=coordinates[3], maxZ=coordinates[4];
            minX=x-r, minY=y-r, maxX=x+r, maxY=y+r;
        }
        if(args.contains("cube"_)) {
            auto coordinates = toIntegers(args.at("cube"_));
            minX=coordinates[0], minY=coordinates[1], minZ=coordinates[2], maxX=coordinates[3], maxY=coordinates[4], maxZ=coordinates[5];
        }
        assert_(minX<maxX && minY<maxY && minZ<maxZ && maxX<=image.width && maxY<=image.height && maxZ<=slices.size);
        return (maxX-minX)*(maxY-minY)*(maxZ-minZ)*outputSampleSize(0);
    }

    void execute(const map<ref<byte>, Variant>& args, array<Volume>& outputs, const ref<Volume>&) {
        Folder folder = args.at("source"_);
        array<string> slices = folder.list(Files);

        Volume& target = outputs.first();
        target.sampleCount = int3(maxX-minX, maxY-minY, maxZ-minZ);
        target.maximum = (1<<(target.sampleSize*8))-1;
        uint X = target.sampleCount.x, Y = target.sampleCount.y, Z = target.sampleCount.z, XY=X*Y;
        Time time; Time report;
        uint16* const targetData = (Volume16&)outputs.first();
        for(uint z: range(Z)) {
            if(report/1000>=2) { log(z,"/",Z, (z*XY*2/1024/1024)/(time/1000), "MB/s"); report.reset(); } // Reports progress every 2 second (initial read from a cold drive may take minutes)
            Tiff16(Map(slices[minZ+z],folder)).read(targetData+z*XY, minX, minY, maxX-minX, maxY-minY); // Directly decodes slice images into the volume
        }
    }
};
