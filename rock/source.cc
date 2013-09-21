#include "volume-operation.h"
#include "time.h"
#include "tiff.h"
#include "crop.h"
//#include "bmp.h"

/// Concatenates image slice files in a volume
/// \note Parses physical resolution from source path
class(Source, Operation), virtual VolumeOperation {
    CropVolume crop;

    string parameters() const override { return "path resolution cylinder box downsample extra"_; }
    uint outputSampleSize(const Dict& args, const ref<Result*>&, uint index) override {
        if(index) return 0; // Extra outputs
        assert_(args.contains("path"_), args);
        string path = args.at("path"_);
        if(!existsFolder(path, currentWorkingDirectory())) { // Volume source
            TextData s (path); if(path.contains('}')) s.whileNot('}'); s.until('.'); string metadata = s.untilEnd();
            Map file(path, currentWorkingDirectory());
            Volume volume = toVolume(metadata, buffer<byte>(file));
            assert(volume.sampleSize);
            return volume.sampleSize;
        } else { // Image slices
            Folder folder = Folder(path, currentWorkingDirectory());
            array<String> slices = folder.list(Files|Sorted);
            assert_(slices, path);
            Map file (slices.first(), folder);
            return isTiff(file) ? 2 : 1; // Assumes TIFF (only) are 16bit
        }
    }
    size_t outputSize(const Dict& args, const ref<Result*>& inputs, uint index) override {
        if(index) return 0;
        int3 size;
        assert_(args.contains("path"_), args);
        string path = args.at("path"_);
        if(!existsFolder(path, currentWorkingDirectory())) {
            TextData s (path); if(path.contains('}')) s.whileNot('}'); s.until('.'); string metadata = s.untilEnd();
            Volume volume;
            if(!parseVolumeFormat(volume, metadata)) error("Unknown format");
            crop = parseCrop(args, volume.margin, volume.sampleCount-volume.margin, args.contains("extra"_)?2:0);
        } else {
            Folder folder = Folder(path, currentWorkingDirectory());
            array<String> slices = folder.list(Files|Sorted);
            assert_(slices, path);
            size.z=slices.size;
            Map file (slices.first(), folder);
            if(isTiff(file)) { const Tiff16 image (file); size.x=image.width,  size.y=image.height; }
            else { Image image = decodeImage(file); assert_(image, path, slices.first());  size.x=image.width, size.y=image.height; }
            crop = parseCrop(args, 0, size, args.contains("extra"_)?2:0 /*HACK: Enlarges crop volume slightly to compensate margins lost to median and skeleton*/);
        }
        assert(crop.sampleCount);
        return (uint64)crop.sampleCount.x*crop.sampleCount.y*crop.sampleCount.z*outputSampleSize(args, inputs, 0);
    }
    void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>&, const mref<Result*>& otherOutputs) override { //FIXME: template for 8bit vs 16bit sample size
        assert_(crop.size);
        int3 min=crop.min, size=crop.size;
        string path = args.at("path"_);
        assert(outputs);
        Volume& target = outputs.first();
        target.sampleCount = crop.sampleCount;
        target.margin = crop.margin;
        target.field = String("μ"_); // Radiodensity
        target.origin = crop.min-target.margin;
        const uint64 X= target.sampleCount.x, Y= target.sampleCount.y;
        const int64 marginX = target.margin.x, marginY = target.margin.y, marginZ = target.margin.z;
        Time time; Time report;
        if(!existsFolder(path, currentWorkingDirectory())) {
            TextData s (path); if(path.contains('}')) s.whileNot('}'); s.until('.'); string metadata = s.untilEnd();
            Map file(path, currentWorkingDirectory()); // Copy from disk to process managed memory
            Volume source = toVolume(metadata, buffer<byte>(file));
            const uint64 sX = source.sampleCount.x, sY = source.sampleCount.y;

            if(args.value("downsample"_,"0"_)!="0"_) { // Streaming downsample (works for larger than RAM source volumes)
                if(source.sampleSize==2) { // Reads from disk into process managed cache (16bit)
                    uint16* const targetData = (Volume16&)target;
                    for(uint z: range(size.z)) {
                        if(report/1000>=5) { log(z,"/",size.z, (z*size.x*size.y/1024/1024)/(time/1000), "MS/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
                        uint16* const sourceSlice = (uint16*)source.data.data + (min.z+z)*sX*sY;
                        uint16* const targetSlice = targetData + (marginZ+z)*X*Y + marginY*X + marginX;
                        for(uint y: range(size.y)) for(uint x: range(size.x)) {
                            uint16* const source = sourceSlice + (min.y+y)*2*sX+(min.x+x)*2;
                            targetSlice[y*X+x] = (source[0] + source[1] + source[sX*2] + source[sX+1] +
                                    source[sX*sY+0] + source[sX*sY+1] + source[sX*sY+sX] + source[sX*sY+sX+1])/8;
                        }
                    }
                } else if(source.sampleSize==1) {
                    uint8* const targetData = (Volume8&)target;
                    for(uint z: range(size.z)) {
                        if(report/1000>=5) { log(z,"/",size.z, (z*size.x*size.y/1024/1024)/(time/1000), "MS/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
                        uint8* const sourceSlice = (uint8*)source.data.data + (min.z+z)*sX*sY;
                        uint8* const targetSlice = targetData + (marginZ+z)*X*Y + marginY*X + marginX;
                        for(uint y: range(size.y)) for(uint x: range(size.x)) {
                            uint8* const source = sourceSlice + (min.y+y)*2*sX+(min.x+x)*2;
                            targetSlice[y*X+x] = (source[0] + source[1] + source[sX*2] + source[sX+1] +
                                    source[sX*sY+0] + source[sX*sY+1] + source[sX*sY+sX] + source[sX*sY+sX+1])/8;
                        }
                    }
                } else error(source.sampleSize);
            } else if(source.sampleSize==2) { // Reads from disk into process managed cache (16bit)
                uint16* const targetData = (Volume16&)target;
                for(uint z: range(size.z)) {
                    if(report/1000>=5) { log(z,"/",size.z, (z*size.x*size.y/1024/1024)/(time/1000), "MS/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
                    uint16* const sourceSlice = (uint16*)source.data.data + (min.z+z)*sX*sY;
                    uint16* const targetSlice = targetData + (marginZ+z)*X*Y + marginY*X + marginX;
                    for(uint y: range(size.y)) for(uint x: range(size.x)) targetSlice[y*X+x] = sourceSlice[(min.y+y)*sX+min.x+x];
                }
            } else if(source.sampleSize==1) { // Reads from disk into process managed cache (8bit)
                uint8* const targetData = (Volume8&)target;
                for(uint z: range(size.z)) {
                    if(report/1000>=5) { log(z,"/",size.z, (z*size.x*size.y/1024/1024)/(time/1000), "MS/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
                    uint8* const sourceSlice = (uint8*)source.data.data + (min.z+z)*sX*sY;
                    uint8* const targetSlice = targetData + (marginZ+z)*X*Y + marginY*X + marginX;
                    for(uint y: range(size.y)) for(uint x: range(size.x)) targetSlice[y*X+x] = sourceSlice[(min.y+y)*sX+min.x+x];
                }
            } else error(source.sampleSize);
        } else {
            Folder folder = Folder(path, currentWorkingDirectory());
            array<String> slices = folder.list(Files|Sorted);
            if(target.sampleSize==2) {
                uint16* const targetData = (Volume16&)target;
                if(args.value("downsample"_,"0"_)!="0"_) { // Streaming downsample (works for larger than RAM source volumes)
                    const uint64 sX = X*2, sY = Y*2;
                    buffer<uint16> sliceBuffer(2*sX*sY);
                    for(uint z: range(size.z)) {
                        if(report/1000>=5) { log(z,"/",size.z, (8*z*size.x*size.y/1024/1024)/(time/1000), "MS/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
                        uint16* const targetSlice = targetData + (marginZ+z)*X*Y + marginY*X + marginX;
                        for(uint i: range(2)) {
                            Map file(slices[(min.z+z)*2+i],folder);
                            assert_(isTiff(file)); // Directly decodes slice images into the slice buffer
                            Tiff16 tiff(file); assert_(tiff);
                            tiff.read(sliceBuffer.begin()+i*sX*sY, min.x*2, min.y*2, size.x*2, size.y*2, sX);
                        }
                        for(uint y: range(size.y)) for(uint x: range(size.x)) {
                            uint16* const source = sliceBuffer.begin() + (y*2)*sX+(x*2);
                            targetSlice[y*X+x] = (source[0] + source[1] + source[sX] + source[sX+1] +
                                    source[sX*sY+0] + source[sX*sY+1] + source[sX*sY+sX] + source[sX*sY+sX+1])/8;
                        }
                    }
                } else {
                    for(uint z: range(size.z)) {
                        if(report/1000>=5) { log(z,"/",size.z, (z*size.x*size.y/1024/1024)/(time/1000), "MS/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
                        uint16* const targetSlice = targetData + (marginZ+z)*X*Y + marginY*X + marginX;
                        Map file(slices[min.z+z], folder);
                        assert_(isTiff(file)); // Directly decodes slice images into the volume
                        Tiff16 tiff(file);
                        assert_(tiff, path, slices[min.z+z]);
                        tiff.read(targetSlice, min.x, min.y, size.x, size.y, X);
                    }
                }
            } else if(target.sampleSize==1) {
                uint8* const targetData = (Volume8&)target;
                if(args.value("downsample"_,"0"_)!="0"_) { // Streaming downsample (works for larger than RAM source volumes)
                    const uint64 sX = X*2, sY = Y*2;
                    buffer<uint8> sliceBuffer(2*sX*sY);
                    for(uint z: range(size.z)) {
                        if(report/1000>=5) { log(z,"/",size.z, (8*z*size.x*size.y/1024/1024)/(time/1000), "MS/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
                        uint8* const targetSlice = targetData + (marginZ+z)*X*Y + marginY*X + marginX;
                        for(uint i: range(2)) {
                            Map file(slices[(min.z+z)*2+i],folder);
                            assert_(!isTiff(file)); // Use generic image decoder (FIXME: Unnecessary (and lossy for >8bit images) roundtrip to 8bit RGBA)
                            Image image = decodeImage(file);
                            assert_(int2(min.x,min.y)+image.size()>=size.xy(), slices[min.z+z]);
                            for(uint y: range(size.y*2)) for(uint x: range(size.x*2)) sliceBuffer[i*sX*sY+y*sX+x] = image(min.x*2+x, min.y*2+y).b;
                        }
                        for(uint y: range(size.y)) for(uint x: range(size.x)) {
                            uint8* const source = sliceBuffer.begin() + (y*2)*sX+(x*2);
                            targetSlice[y*X+x] = (source[0] + source[1] + source[sX] + source[sX+1] +
                                    source[sX*sY+0] + source[sX*sY+1] + source[sX*sY+sX] + source[sX*sY+sX+1])/8;
                        }
                    }
                } else {
                    for(uint z: range(size.z)) {
                        if(report/1000>=5) { log(z,"/",size.z, (z*size.x*size.y/1024/1024)/(time/1000), "MS/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
                        uint8* const targetSlice = targetData + (marginZ+z)*X*Y + marginY*X + marginX;
                        Map file(slices[min.z+z], folder);
                        assert_(!isTiff(file)); // Use generic image decoder (FIXME: Unnecessary (and lossy for >8bit images) roundtrip to 8bit RGBA)
                        Image image = decodeImage(file);
                        assert_(int2(min.x,min.y)+image.size()>=size.xy(), slices[min.z+z]);
                        for(uint y: range(size.y)) for(uint x: range(size.x)) targetSlice[y*X+x] = image(min.x+x, min.y+y).b;
                    }
                }
            } else error(target.sampleSize);
        }
        if(target.sampleSize==1) target.maximum = (1<<(target.sampleSize*8))-1; //FIXME: target.maximum = maximum((Volume8&)target); // Some sources don't use the full range
        if(target.sampleSize==2) target.maximum = maximum((Volume16&)target); // Some sources don't use the full range
        assert_(target.maximum, target.sampleCount, target.margin);
        {TextData s (args.at("path"_));
            string name = s.until('-');
            output(otherOutputs, "name"_, "label"_, [&]{return name+"\n"_;});
            float resolution = s ? s.decimal()/1000.0 : args.contains("resolution"_) ? toDecimal(args.at("resolution"_)) : 1; if(args.contains("downsample"_)) resolution *= 2;
            output(otherOutputs, "resolution"_, "scalar"_, [&]{return str(resolution)+" μm\n"_;});
            int3 voxelSize = target.sampleCount-2*target.margin;
            output(otherOutputs, "voxelSize"_, "size"_, [&]{return str(voxelSize.x)+"x"_+str(voxelSize.y)+"x"_+str(voxelSize.z) + " voxels"_;});
            int3 physicalSize = int3(round(resolution*vec3(voxelSize)));
            output(otherOutputs, "physicalSize"_, "size"_, [&]{return str(physicalSize.x)+"x"_+str(physicalSize.y)+"x"_+str(physicalSize.z)+" μm³"_;});
        }
    }
};
