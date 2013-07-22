#include "volume-operation.h"
#include "time.h"
#include "tiff.h"
#include "crop.h"
//#include "bmp.h"

/// Concatenates image slice files in a volume
/// \note Parses physical resolution from source path
class(Source, Operation), virtual VolumeOperation {
    CropVolume crop;

    string parameters() const override { return "path resolution cylinder downsample extra"_; }
    uint outputSampleSize(uint index) override { return index ? 0 : 2; }
    size_t outputSize(const Dict& args, const ref<Result*>&, uint index) override {
        if(index) return 0;
        int3 size;
        assert_(args.contains("path"_), args);
        string path = args.at("path"_);
        if(!existsFolder(path, currentWorkingDirectory())) {
            TextData s (path); if(path.contains('}')) s.whileNot('}'); s.until('.'); string metadata = s.untilEnd();
            Volume volume;
            if(!parseVolumeFormat(volume, metadata)) error("Unknown format");
            crop = parseCrop(args, volume.margin, volume.sampleCount-volume.margin);
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
        return (uint64)crop.sampleCount.x*crop.sampleCount.y*crop.sampleCount.z*outputSampleSize(0);
    }
    void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>&, const mref<Result*>& otherOutputs) override {
        int3 min=crop.min, size=crop.size;
        string path = args.at("path"_);
        Volume16& target = outputs.first();
        target.sampleCount = crop.sampleCount;
        target.margin = crop.margin;
        target.field = String("μ"_); // Radiodensity
        target.origin = crop.min-target.margin;
        const uint64 X= target.sampleCount.x, Y= target.sampleCount.y;
        const int64 marginX = target.margin.x, marginY = target.margin.y, marginZ = target.margin.z;
        Time time; Time report;
        uint16* const targetData = (Volume16&)outputs.first();
        if(!existsFolder(path, currentWorkingDirectory())) {
            assert_(args.value("downsample"_,"0"_)=="0"_);
            TextData s (path); if(path.contains('}')) s.whileNot('}'); s.until('.'); string metadata = s.untilEnd();
            Volume source;
            if(!parseVolumeFormat(source, metadata)) error("Unknown format");
            uint64 sX = source.sampleCount.x, sY = source.sampleCount.y, unused sZ = source.sampleCount.z;

            Map file(path, currentWorkingDirectory()); // Copy from disk to process managed memory
            for(uint z: range(size.z)) {
                if(report/1000>=5) { log(z,"/",size.z, (z*size.x*size.y/1024/1024)/(time/1000), "MS/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
                uint16* const sourceSlice = (uint16*)file.data.pointer + (min.z+z)*sX*sY;
                uint16* const targetSlice = targetData + (marginZ+z)*X*Y + marginY*X + marginX;
                for(uint y: range(size.y)) for(uint x: range(size.x)) targetSlice[y*X+x] = sourceSlice[(min.y+y)*sX+min.x+x];
            }
        } else {
            Folder folder = Folder(path, currentWorkingDirectory());
            array<String> slices = folder.list(Files|Sorted);
            if(args.value("downsample"_,"0"_)!="0"_) { // Streaming downsample for larger than RAM volumes
                const uint sliceStride = Y*2*X*2;
                buffer<uint16> sliceBuffer(2*sliceStride);
                for(uint z: range(size.z)) {
                    if(report/1000>=5) { log(z,"/",size.z, (8*z*size.x*size.y/1024/1024)/(time/1000), "MS/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
                    uint16* const targetSlice = targetData + (marginZ+z)*X*Y + marginY*X + marginX;
                    for(uint i: range(2)) {
                        Map file(slices[(min.z+z)*2+i],folder);
                        Tiff16 tiff(file); assert_(tiff);
                        tiff.read(sliceBuffer.begin()+i*sliceStride, min.x*2, min.y*2, size.x*2, size.y*2, X*2);
                    }
                    for(uint y: range(size.y)) for(uint x: range(size.x)) {
                        uint16* const source = sliceBuffer.begin() + (y*2)*X*2+(x*2);
                        targetSlice[y*X+x] = (source[0] + source[1] + source[X*2] + source[X*2+1] +
                                source[sliceStride+0] + source[sliceStride+1] + source[sliceStride+X*2] + source[sliceStride+X*2+1])/8;
                    }
                }
            }
            for(uint z: range(size.z)) {
                if(report/1000>=5) { log(z,"/",size.z, (z*size.x*size.y/1024/1024)/(time/1000), "MS/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
                uint16* const targetSlice = targetData + (marginZ+z)*X*Y + marginY*X + marginX;
                Map file(slices[min.z+z], folder);
                if(isTiff(file)) { // Directly decodes slice images into the volume
                    Tiff16 tiff(file);
                    assert_(tiff, path, slices[min.z+z]);
                    tiff.read(targetSlice, min.x, min.y, size.x, size.y, X);
                } else { // Use generic image decoder (FIXME: Unnecessary (and lossy for >8bit images) roundtrip to 8bit RGBA)
                    Image image = decodeImage(file);
                    assert_(int2(min.x,min.y)+image.size()>=size.xy(), slices[min.z+z]);
                    for(uint y: range(size.y)) for(uint x: range(size.x)) targetSlice[y*X+x] = image(min.x+x, min.y+y).b;
                }
            }
        }
        target.maximum = maximum(target); // Some sources don't use the full range
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
