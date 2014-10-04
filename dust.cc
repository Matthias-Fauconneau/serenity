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
    //float max = ::max(source.pixels);
    for(uint y: range(source.size.y)) for(uint x: range(source.size.x)) {
        float v = source(x,y); // max;
        assert_(v>0 && v <= 1, v);
        uint linear12 = 0xFFF*v;
        extern uint8 sRGB_forward[0x1000];
        assert_(linear12 < 0x1000);
        uint8 sRGB = sRGB_forward[linear12];
        target(x,y) = byte4(sRGB, sRGB, sRGB, 0xFF);
    }
    return move(target);
}


#include "thread.h"
#include "file.h"
#include "image.h"
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

/// Collection of processed images
struct Filter {
    Folder folder = Folder("Pictures/Paper"_, home());
    Folder cache = Folder(".cache"_, folder, true);
    Folder targetFolder = Folder(".target"_, folder, true);

    /// Lists matching images
    array<String> listImages() {
        array<String> imageNames;
        array<String> fileNames = folder.list(Files|Sorted);
        for(String& fileName: fileNames) {
            Map file = Map(fileName, folder);
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
    ImageSource sourceImage(string imageName) const {
        // Caches conversion from sRGB JPEGs to raw (mmap'able) linear float images
        string baseName = section(imageName,'.');
        if(/*1 ||*/ !existsFile(baseName, cache)) { //FIXME: automatic invalidation
            log_(imageName);
            Image source = decodeImage(Map(imageName, folder));

            log(" ->",baseName);
            ImageTarget target (baseName, cache, source.size);
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
        return ImageSource(baseName, cache, imageSize);
    }

    /// Returns filtered image
    virtual ImageSource cleanImage(string imageName) const abstract;
};

/// \file dust.cc Automatic dust removal
#include "interface.h"
#include "window.h"

struct DustRemoval : Filter {
    /// Sums all images
    ImageSource evaluateSum() {
        if(/*1 ||*/ !existsFile("sum"_, cache)) { //FIXME: automatic invalidation
            ImageTarget sum ("sum"_, cache, imageSize);
            assert_(!::sum(sum.pixels));
            for(string imageName: imageNames) {
                ImageSource source = sourceImage(imageName);
                chunk_parallel(source.pixels.size, [&](uint, uint start, uint size) {
                    for(uint index: range(start, start+size)) {
                        sum.pixels[index] += source.pixels[index];
                    }
                });
            }

            // Normalizes sum by maximum (TODO: normalize by low frequency energy)
            float maximums[threadCount];
            chunk_parallel(sum.pixels.size, [&](uint id, uint start, uint size) {
                float max=0;
                for(uint index: range(start, start+size)) { float v=sum.pixels[index]; if(v>max) max = v; }
                maximums[id] = max;
            });
            float maximum = max(maximums);
            chunk_parallel(sum.pixels.size, [&](uint, uint start, uint size) {
                float scaleFactor = 1./maximum;
                for(uint index: range(start, start+size)) sum.pixels[index] *= scaleFactor;
            });
            // TODO: Band pass spot (low pass to filter texture and noise, high pass to filter lighting conditions)
        }
        return ImageSource("sum"_, cache, imageSize);
    }

    ImageSource sum = evaluateSum();

    ImageSource cleanImage(string imageName) const override {
        ImageSource source = sourceImage(imageName);
        if(/*1 ||*/ !existsFile("imageName"_, targetFolder)) { //FIXME: automatic invalidation
            ImageTarget target (imageName, targetFolder, source.ImageF::size);
            // Removes dust from image
            chunk_parallel(target.pixels.size, [&](uint, uint start, uint size) {
                for(uint index: range(start, start+size)) target.pixels[index] = source.pixels[index] / sum.pixels[index];
            });
        }
        return  ImageSource(imageName, targetFolder, source.ImageF::size);
    }
};

struct FilterView : ImageWidget {
    const Filter& filter;
    string imageName = filter.imageNames[0];
    bool enabled = true;
    Image image() const { return sRGB(enabled ? (ImageF)filter.cleanImage(imageName) : (ImageF)filter.sourceImage(imageName)); }

    FilterView(const Filter& filter) : filter(filter) { ImageWidget::image = image(); }

    bool mouseEvent(int2 cursor, int2 size, Event, Button, Widget*&) override {
        string imageName = filter.imageNames[(filter.imageNames.size-1)*cursor.x/size.x];
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

struct DustRemover {
    DustRemoval filter;
    FilterView view {filter};
    Window window {&view, filter.imageSize/4, str(view.imageName, view.enabled)};
} application;
