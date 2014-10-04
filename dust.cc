/// \file parallel.cc Parallel operations

#include "thread.h"
float maximum(ref<float> values) {
    float maximums[threadCount];
    chunk_parallel(values.size, [&](uint id, uint start, uint size) {
        float max=0;
        for(uint index: range(start, start+size)) { float v=values[index]; if(v>max) max = v; }
        maximums[id] = max;
    });
    return max(maximums);
}

/// \file .h  ImageF
#include "image.h"

/// 2D array of floating-point pixels
struct ImageF {
    ImageF(){}
    ImageF(buffer<float>&& data, int2 size) : pixels(move(data)), size(size) { assert_(pixels.size==size_t(size.x*size.y)); }
    ImageF(int width, int height) : width(width), height(height) { assert_(size>int2(0)); pixels=::buffer<float>(width*height); }
    ImageF(int2 size) : ImageF(size.x, size.y) {}

    explicit operator bool() const { return pixels && width && height; }
    inline float& operator()(uint x, uint y) const {assert(x<uint(size.x) && y<uint(size.y)); return pixels[y*size.x+x]; }

    buffer<float> pixels;
    union {
        struct { uint width, height; };
        int2 size;
    };
};

/// Converts a linear float image to sRGB
Image sRGB(Image&& target, const ImageF& source);
Image sRGB(const ImageF& source) { return sRGB(source.size, source); }


/// \file .cc ImageF
Image sRGB(Image&& target, const ImageF& source) {
    //float min = ::min(source.pixels), max = ::max(source.pixels); log(min, max);
    for(uint y: range(source.size.y)) for(uint x: range(source.size.x)) {
        float v = source(x,y);
        if(1) v = ::min(1.f, v); // Clip
        /*else { // Normalize
            v = (v-min)/(max-min);
            assert_(v <= 1);
        }*/
        assert_(v>=0, v);
        uint linear12 = 0xFFF*v;
        extern uint8 sRGB_forward[0x1000];
        assert_(linear12 < 0x1000);
        uint8 sRGB = sRGB_forward[linear12];
        target(x,y) = byte4(sRGB, sRGB, sRGB, 0xFF);
    }
    return move(target);
}

#include "file.h"
#include "exif.h"
#include "jpeg.h"

struct ImageTarget : Map, ImageF {
    ImageTarget(const string& path, const Folder& at, int2 size) :
        Map(File(path, at, ::Flags(ReadWrite|Create)).resize(size.x*size.y*sizeof(float)), Map::Write),
        ImageF(unsafeReference(cast<float>((Map&)*this)), size) {}
};

struct ImageSource : Map, ImageF {
    ImageSource(const string& path, const Folder& at, int2 size) :
        Map (path, at),
        ImageF(unsafeReference(cast<float>((Map&)*this)), size) {}
};

/// Collection of images
struct ImageFolder {
    Folder sourceFolder;
    Folder cacheFolder {".cache"_, sourceFolder, true};

    ImageFolder(Folder&& sourceFolder) : sourceFolder(move(sourceFolder)) {}

    /// Lists matching images
    array<String> listImages() {
        array<String> imageNames;
        array<String> fileNames = sourceFolder.list(Files|Sorted);
        for(String& fileName: fileNames) {
            Map file = Map(fileName, sourceFolder);
            if(imageFileFormat(file)!="JPEG"_) continue; // Only JPEG images
            if(parseExifTags(file).at("Exif.Photo.FNumber"_).real() != 6.3) continue; // Only same aperture //FIXME: -> DustRemoval
            //TODO: if(source.size != imageSize) { log("Warning: inconsistent source image size"); continue; }
            imageNames << move(fileName);
        }
        return imageNames;
    }

    array<String> imageNames = listImages();
    const int2 imageSize = int2(4000, 3000); //FIXME: = ::imageSize(readFile(imageNames.first()));

    /// Loads linear float image
    ImageSource image(string imageName) const {
        // Caches conversion from sRGB JPEGs to raw (mmap'able) linear float images
        string baseName = section(imageName,'.');
        if(/*1 ||*/ !existsFile(baseName, cacheFolder)) { //FIXME: automatic invalidation
            log_(imageName);
            Image source = decodeImage(Map(imageName, sourceFolder));

            log(" ->",baseName);
            ImageTarget target (baseName, cacheFolder, source.size);
            chunk_parallel(source.pixels.size, [&](uint, uint start, uint size) {
                for(uint index: range(start, start+size)) {
                    byte4 sRGB = source.pixels[index];
                    float b = sRGB_reverse[sRGB.b];
                    float g = sRGB_reverse[sRGB.g];
                    float r = sRGB_reverse[sRGB.r];
                    float intensity = (b+g+r)/3; // Assumes dust affects all components equally
                    target.pixels[index] = intensity;
                }
            });
            assert_(::sum(target.pixels));
        }
        return ImageSource(baseName, cacheFolder, imageSize);
    }
};

struct Filter {
    /// Returns filtered image
    virtual ImageSource image(const ImageFolder& folder, string imageName) const abstract;
};


/// \file dust.cc Automatic dust removal
#include "interface.h"
#include "window.h"

/// Inverts attenuation bias
struct InverseAttenuation : Filter {
    ImageSource attenuationBias;

    InverseAttenuation(const ImageFolder& calibrationImages) : attenuationBias(calibrateAttenuationBias(calibrationImages)) {}

    /// Calibrates attenuation bias image by summing images of a white subject
    ImageSource calibrateAttenuationBias(const ImageFolder& calibration) {
        if(1 || !existsFile("attenuationBias"_, calibration.cacheFolder)) { //FIXME: automatic invalidation
            ImageTarget sum ("attenuationBias"_, calibration.cacheFolder, calibration.imageSize);
            sum.pixels.clear();

            for(string imageName: calibration.imageNames) {
                ImageSource source = calibration.image(imageName);
                chunk_parallel(source.pixels.size, [&](uint, uint start, uint size) {
                    for(uint index: range(start, start+size)) {
                        sum.pixels[index] += source.pixels[index];
                    }
                });
            }
            // Low pass to filter texture and noise
            //sum = gaussianBlur(move(sum));
            // TODO: High pass to filter lighting conditions
            // Normalizes sum by maximum (TODO: normalize by low frequency energy)
            float max = maximum(sum.pixels);
            //log(min(sum.pixels), max);
            chunk_parallel(sum.pixels.size, [&](uint, uint start, uint size) {
                float scaleFactor = 1./max;
                for(uint index: range(start, start+size)) sum.pixels[index] *= scaleFactor;
            });
        }
        return ImageSource("attenuationBias"_, calibration.cacheFolder, calibration.imageSize);
    }

    ImageSource image(const ImageFolder& imageFolder, string imageName) const override {
        Folder targetFolder = Folder(".target"_, imageFolder.cacheFolder, true);
        ImageSource source = imageFolder.image(imageName);
        if(/*1 ||*/ !existsFile(imageName, targetFolder)) { //FIXME: automatic invalidation
            ImageTarget target (imageName, targetFolder, source.ImageF::size);
            // Removes dust from image
            chunk_parallel(target.pixels.size, [&](uint, uint start, uint size) {
                for(uint index: range(start, start+size)) target.pixels[index] = source.pixels[index] / attenuationBias.pixels[index];
            });
        }
        return  ImageSource(imageName, targetFolder, source.ImageF::size);
    }
};

struct FilterView : ImageWidget {
    const Filter& filter;
    const ImageFolder& images;

    string imageName = images.imageNames[0];
    bool enabled = true;
    Image image() const { return sRGB(enabled ? (ImageF)filter.image(images, imageName) : (ImageF)images.image(imageName)); }

    FilterView(const Filter& filter, const ImageFolder& images) : filter(filter), images(images) { ImageWidget::image = image(); }

    bool mouseEvent(int2 cursor, int2 size, Event, Button, Widget*&) override {
        string imageName = images.imageNames[(images.imageNames.size-1)*cursor.x/size.x];
        bool enabled = cursor.y < size.y/2;
        if(enabled != this->enabled || imageName != this->imageName) {
            this->enabled = enabled;
            this->imageName = imageName;
            ImageWidget::image = image();
            return true;
        }
        return false;
    }
};

struct DustRemovalPreview {
    InverseAttenuation filter {Folder("Pictures/Paper"_, home())};
    ImageFolder images {Folder("Pictures/Paper"_, home())};
    //FilterView view {filter, images};
    ImageWidget attenuationBias {sRGB(filter.attenuationBias)};
    Window window {&attenuationBias, images.imageSize/4};
} application;
