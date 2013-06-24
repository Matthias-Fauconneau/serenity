#include "volume-operation.h"
#include "time.h"
#include "tiff.h"
//#include "bmp.h"
#include "sample.h"
#include "crop.h"

/// Parses physical resolution from source path
class(PhysicalResolution, Operation) {
    string parameters() const override { return "path downsample"_; }
    void execute(const Dict& args, const ref<Result*>& outputs, const ref<Result*>&) override {
        assert_(args.contains("path"_), args);
        string resolutionMetadata = section(args.at("path"_),'-',1,2);
        double resolution = resolutionMetadata ? TextData(resolutionMetadata).decimal()/1000.0 : 1;
        {string times = args.value("downsample"_,"0"_); resolution *= pow(2, toInteger(times?:"1"_));}
        outputs[0]->metadata = String("scalar"_);
        outputs[0]->data = str(resolution)+"\n"_;
    }
};

/// Concatenates image slice files in a volume
class(Source, Operation), virtual VolumeOperation {
    CropVolume crop;

    string parameters() const override { static auto p="path cylinder box downsample"_; return p; }
    uint outputSampleSize(uint) override { return 2; }
    size_t outputSize(const Dict& args, const ref<Result*>& inputs, uint) override {
        int3 size;
        assert_(args.contains("path"_), args);
        string path = args.at("path"_);
        if(!existsFolder(path, currentWorkingDirectory())) {
            TextData s (path); if(path.contains('}')) s.whileNot('}'); s.until('.'); string metadata = s.untilEnd();
            Volume volume;
            if(!parseVolumeFormat(volume, metadata)) error("Unknown format");
            size=volume.sampleCount;
        } else {
            Folder folder = Folder(path, currentWorkingDirectory());
            array<String> slices = folder.list(Files|Sorted);
            assert_(slices, path);
            size.z=slices.size;
            Map file (slices.first(), folder);
            if(isTiff(file)) { const Tiff16 image (file); size.x=image.width,  size.y=image.height; }
            else { Image image = decodeImage(file); assert_(image, path, slices.first());  size.x=image.width, size.y=image.height; }
        }
        crop = parseCrop(args, int3(0), size, inputs?inputs[0]->data:""_); // input argument (from automatic crop (CommonSampleSize))
        return (uint64)crop.sampleCount.x*crop.sampleCount.y*crop.sampleCount.z*outputSampleSize(0);
    }
    void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>&) override {
        int3 min=crop.min, size=crop.size;
        string path = args.at("path"_);
        Volume16& target = outputs.first();
        target.sampleCount = crop.sampleCount;
        target.margin = crop.margin;
        uint X = target.sampleCount.x, Y = target.sampleCount.y, Z = target.sampleCount.z;
        uint marginX = target.margin.x, marginY = target.margin.y, marginZ = target.margin.z;
        Time time; Time report;
        uint16* const targetData = (Volume16&)outputs.first();
        if(!existsFolder(path, currentWorkingDirectory())) {
            assert_(args.value("downsample"_,"0"_)=="0"_);
            TextData s (path); if(path.contains('}')) s.whileNot('}'); s.until('.'); string metadata = s.untilEnd();
            Volume source;
            if(!parseVolumeFormat(source, metadata)) error("Unknown format");
            uint sX = source.sampleCount.x, sY = source.sampleCount.y, unused sZ = source.sampleCount.z;

            Map file(path, currentWorkingDirectory()); // Copy from disk mapped to process managed memory
            for(uint z: range(size.z)) {
                if(report/1000>=5) { log(z,"/",Z, (z*X*Y*2/1024/1024)/(time/1000), "MB/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
                uint16* const sourceSlice = (uint16*)file.data.pointer + (min.z+z)*sX*sY;
                uint16* const targetSlice = targetData + (uint64)(marginZ+z)*X*Y + marginY*X + marginX;
                for(uint y: range(size.y)) for(uint x: range(size.x)) targetSlice[y*X+x] = sourceSlice[(min.y+y)*sX+min.x+x];
            }
        } else {
            Folder folder = Folder(path, currentWorkingDirectory());
            array<String> slices = folder.list(Files|Sorted);
            for(uint z: range(size.z)) {
                if(report/1000>=5) { log(z,"/",Z, (z*X*Y*2/1024/1024)/(time/1000), "MB/s"); report.reset(); } // Reports progress (initial read from a cold drive may take minutes)
                uint16* const targetSlice = targetData + (uint64)(marginZ+z)*X*Y + marginY*X + marginX;
                if(args.value("downsample"_,"0"_)!="0"_) { // Streaming downsample for larger than RAM volumes
                    const uint sliceStride = Y*2*X*2;
                    buffer<uint16> sliceBuffer(2*sliceStride);
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
                } else {
                    Map file(slices[min.z+z],folder);
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
        }
        target.maximum = maximum(target); // Some sources don't use the full range
        assert_(target.maximum, target.sampleCount, target.margin);
    }
};

/// Returns largest possible box fitting all inputs
class(CommonSampleSize, Operation), virtual Pass {
    virtual void execute(const Dict& arguments, Result& size, const Result& resolutions) override {
        assert_(endsWith(resolutions.metadata,"tsv"_), resolutions.metadata, resolutions.data);
        ScalarMap inputs = parseMap(resolutions.data);
        array<vec3> physicalSampleSizes;
        for(auto input: inputs) {
            Dict args = copy(arguments);
            if(args.contains("path"_)) args.remove("path"_); //Removes sweep argument
            args.insert(String("path"_), copy(input.key));
            Source source; source.outputSize(args, {}, 0);
            physicalSampleSizes << float(input.value)*vec3(source.crop.size);
        }
        vec3 min = ::min(physicalSampleSizes);
        size.metadata = String("vector"_);
        size.data = toASCII( Vector(ref<double>{min.x, min.y, min.z}) );
    }
};
