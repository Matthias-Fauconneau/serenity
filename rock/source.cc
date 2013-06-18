#include "volume-operation.h"
#include "time.h"
#include "tiff.h"
//#include "bmp.h"

class(CommonSampleSize, Operation) {
    void execute(const Dict&, const ref<Result*>& outputs unused, const ref<Result*>& inputs) override {
        error(inputs[0]->data, inputs[1]->data);
    }
};

/// Concatenates image slice files in a volume
class(Source, Operation), virtual VolumeOperation {
    uint minX, minY, minZ, maxX, maxY, maxZ;
    int3 sampleCount;

    string parameters() const override { static auto p="path cylinder cube sliceDownsample"_; return p; }
    uint outputSampleSize(uint) override { return 2; }
    size_t outputSize(const Dict& args, const ref<Result*>& inputs, uint) override {
        uint width, height, sliceCount;
        string path = args.at("path"_);
        if(!existsFolder(path, currentWorkingDirectory())) {
            TextData s (path); if(path.contains('}')) s.whileNot('}'); s.until('.'); string metadata = s.untilEnd();
            Volume volume;
            if(!parseVolumeFormat(volume, metadata)) error("Unknown format");
            width=volume.sampleCount.x, height=volume.sampleCount.y, sliceCount=volume.sampleCount.z;
        } else {
            Folder folder = Folder(path, currentWorkingDirectory());
            array<String> slices = folder.list(Files|Sorted);
            assert_(slices, path);
            sliceCount=slices.size;
            Map file (slices.first(), folder);
            if(isTiff(file)) { const Tiff16 image (file); width=image.width, height=image.height; }
            else { Image image = decodeImage(file); assert_(image, path, slices.first()); width=image.width, height=image.height; }
        }
        minX=0, minY=0, minZ=0, maxX = width, maxY = height, maxZ = sliceCount;
        if(args.contains("cylinder"_)) {
            if(args.at("cylinder"_)!=""_) {
                if(args.at("cylinder"_).contains(',')) { // x, y, r, zMin, zMax
                    auto coordinates = apply<int64>(split(args.at("cylinder"_),','), toInteger, 10);
                    int x=coordinates[0], y=coordinates[1], r=coordinates[2]; minZ=coordinates[3], maxZ=coordinates[4];
                    minX=x-r, minY=y-r, maxX=x+r, maxY=y+r;
                } else { // Crops centered cylinder
                    int r = toInteger(args.at("cylinder"_));
                    minX=maxX/2-r, minY=maxY/2-r, minZ=maxZ/2-r;
                    maxX=maxX/2+r, maxY=maxY/2+r, maxZ=maxZ/2+r;
                }
            }
            int margin = int(maxX-minX) - int(maxY-minY);
            if(margin > 0) minX+=margin/2, maxX-=margin/2;
            if(margin < 0) minY+=(-margin)/2, maxY-=(-margin)/2;
        }
        string cube;
        if(inputs) cube = inputs[0]->data; // input argument (from automatic crop)
        if(args.contains("cube"_)) cube = args.at("cube"_); // "cube" argument overrides input
        if(cube) {
            if(cube.contains(',')) {
                auto coordinates = apply<int64>(split(cube,','), toInteger, 10);
                minX=coordinates[0], minY=coordinates[1], minZ=coordinates[2], maxX=coordinates[3], maxY=coordinates[4], maxZ=coordinates[5];
            } else { // Crops centered cube
                int side = TextData(cube).integer();
                minX=maxX/2-side/2, minY=maxY/2-side/2, minZ=maxZ/2-side/2;
                maxX=maxX/2+side/2, maxY=maxY/2+side/2, maxZ=maxZ/2+side/2;
            }
        }
        if(args.value("sliceDownsample"_,"0"_)!="0"_) minX /=2, minY /= 2, minZ /= 2, maxX /= 2, maxY /= 2, maxZ /= 2;
        if((maxX-minX)%2) { if(maxX%2) maxX--; else assert(minX%2), minX++; }
        if((maxY-minY)%2) { if(maxY%2) maxY--; else assert(minY%2), minY++; }
        if((maxZ-minZ)%2) { if(maxZ%2) maxZ--; else assert(minZ%2), minZ++; }
        // Asserts valid volume
        assert_(minX<maxX && minY<maxY && minZ<maxZ && maxX<=width && maxY<=height && maxZ<=sliceCount, minX, minY, minZ, maxX, maxY, maxZ, width, height, sliceCount, cube);
        assert_( (maxX-minX)%2 == 0 && (maxY-minY)%2 == 0 && (maxZ-minZ)%2 == 0, minX,minY,minZ, maxX,maxY,maxZ); // Margins are currently always symmetric
        if(args.contains("cylinder"_)) assert_(maxX-minX == maxY-minY, minX, minY, maxX, maxY);
        sampleCount = int3(nextPowerOfTwo(maxX-minX), nextPowerOfTwo(maxY-minY), nextPowerOfTwo(maxZ-minZ));
        while(sampleCount.x < min(sampleCount.y, sampleCount.z)/2) sampleCount.x*=2;
        while(sampleCount.y < min(sampleCount.z, sampleCount.x)/2) sampleCount.y*=2;
        while(sampleCount.z < min(sampleCount.x, sampleCount.y)/2) sampleCount.z*=2;
        return (uint64)sampleCount.x*sampleCount.y*sampleCount.z*outputSampleSize(0);
    }

    void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>&) override {
        string path = args.at("path"_);

        Volume16& target = outputs.first();
        target.sampleCount = sampleCount;
        int3 size = int3(maxX-minX, maxY-minY, maxZ-minZ);
        target.margin = (target.sampleCount - size)/2;
        assert( size+2*target.margin == target.sampleCount );
        uint X = target.sampleCount.x, Y = target.sampleCount.y, Z = target.sampleCount.z;
        uint marginX = target.margin.x, marginY = target.margin.y, marginZ = target.margin.z;
        Time time; Time report;
        uint16* const targetData = (Volume16&)outputs.first();

        if(!existsFolder(path, currentWorkingDirectory())) {
            TextData s (path); if(path.contains('}')) s.whileNot('}'); s.until('.'); string metadata = s.untilEnd();
            Volume source;
            if(!parseVolumeFormat(source, metadata)) error("Unknown format");
            uint sX = source.sampleCount.x, sY = source.sampleCount.y, unused sZ = source.sampleCount.z;

            Map file(path, currentWorkingDirectory()); // Copy from disk mapped to process managed memory
            for(uint z: range(size.z)) {
                assert(minZ+z<sZ);
                uint16* const sourceSlice = (uint16*)file.data.pointer + (minZ+z)*sX*sY;
                uint16* const targetSlice = targetData + (uint64)(marginZ+z)*X*Y + marginY*X + marginX;
                for(uint y: range(size.y)) for(uint x: range(size.x)) targetSlice[y*X+x] = sourceSlice[(minY+y)*sX+minX+x];
            }
        } else {
            Folder folder = Folder(path, currentWorkingDirectory());
            array<String> slices = folder.list(Files|Sorted);
            for(uint z: range(size.z)) {
                if(report/1000>=5) { log(z,"/",Z, (z*X*Y*2/1024/1024)/(time/1000), "MB/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
                uint16* const targetSlice = targetData + (uint64)(marginZ+z)*X*Y + marginY*X + marginX;
                if(args.value("sliceDownsample"_,"0"_)!="0"_) { // Streaming downsample for larger than RAM volumes
                    const uint sliceStride = Y*2*X*2;
                    buffer<uint16> sliceBuffer(2*sliceStride);
                    for(uint i: range(2)) {
                        Map file(slices[minZ+z*2+i],folder);
                        Tiff16 tiff(file); assert_(tiff);
                        tiff.read(sliceBuffer.begin()+i*sliceStride, minX*2, minY*2, size.x*2, size.y*2, X*2);
                    }
                    for(uint y: range(size.y)) for(uint x: range(size.x)) {
                        uint16* const source = sliceBuffer.begin() + (y*2)*X*2+(x*2);
                        targetSlice[y*X+x] = (source[0] + source[1] + source[X*2] + source[X*2+1] +
                                source[sliceStride+0] + source[sliceStride+1] + source[sliceStride+X*2] + source[sliceStride+X*2+1])/8;
                    }
                } else {
                    Map file(slices[minZ+z],folder);
                    if(isTiff(file)) { // Directly decodes slice images into the volume
                        Tiff16 tiff(file);
                        assert_(tiff, path, slices[minZ+z]);
                        tiff.read(targetSlice, minX, minY, size.x, size.y, X);
                    } else { // Use generic image decoder (FIXME: Unnecessary (and lossy for >8bit images) roundtrip to 8bit RGBA)
                        Image image = decodeImage(file);
                        assert_(int2(minX,minY)+image.size()>=size.xy(), slices[minZ+z]);
                        for(uint y: range(size.y)) for(uint x: range(size.x)) targetSlice[y*X+x] = image(minX+x, minY+y).a;
                    }
                }
            }
        }
        target.maximum = maximum(target); // Some sources don't use the full range
    }
};
