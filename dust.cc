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
//inline ImageF share(const ImageF& o) { return ImageF(buffer<float>((ref<float>)o.data),o.size); }

/// Converts a linear float image to sRGB
Image sRGB(Image&& target, const ImageF& source);
Image sRGB(const ImageF& source) { return sRGB(source.size, source); }

/// Downsamples by adding samples
ImageF& downsample(ImageF& target, const ImageF& source);
/// Upsamples an image by duplicating samples
ImageF upsample(const ImageF& source);
//ImageF clip(const ImageF& image, Rect r);

inline void scale(mref<float>& A, float factor) { for(float& a: A) a *= factor; }
inline ImageF scale(ImageF&& image, float factor) { scale(image.pixels, factor); return move(image); }


/// \file .cc ImageF
Image sRGB(Image&& target, const ImageF& source) {
    float max = 0;
    for(uint y: range(source.size.y)) for(uint x: range(source.size.x)) max=::max(max, abs(source(x,y)));
    if(max) for(uint y: range(source.size.y)) for(uint x: range(source.size.x)) {
        float v = source(x,y)/max;
        //assert_(abs(v) <= 1, source(x,y), max);
        v = min(1.f, v);
        uint linear12 = 0xFFF*abs(v);
        extern uint8 sRGB_forward[0x1000];
        assert_(linear12 < 0x1000);
        uint8 sRGB = sRGB_forward[linear12];
        target(x,y) = v > 0 ? byte4(sRGB, sRGB, sRGB, 0xFF) : byte4(sRGB, 0, 0, 0xFF);
    }
    return move(target);
}

ImageF& downsample(ImageF& target, const ImageF& source) {
    for(uint y: range(target.size.y)) for(uint x: range(target.size.x)) target(x,y) = 1.f/4 * (source(x*2+0,y*2+0) + source(x*2+1,y*2+0) + source(x*2+0,y*2+1) + source(x*2+1,y*2+1));
    return target;
}

ImageF upsample(const ImageF& source) {
    ImageF target(source.size*2);
    for(uint y: range(source.size.y)) for(uint x: range(source.size.x)) target(x*2+0,y*2+0) = target(x*2+1,y*2+0) = target(x*2+0,y*2+1) = target(x*2+1,y*2+1) = source(x,y);
    return target;
}

ImageF upsampleY(const ImageF& source) {
    ImageF target(source.size*int2(1,2));
    for(uint y: range(source.size.y)) for(uint x: range(source.size.x)) target(x,y*2+0) = target(x,y*2+0) = target(x,y*2+1) = target(x,y*2+1) = source(x,y);
    return target;
}

/*ImageF clip(const ImageF& image, Rect r) {
    r = r & Rect(image.size);
    assert_(r.size().x == image.size.x, r.size(), image.size);
    return ImageF(buffer<float>(image.data.slice(r.position().y*image.size.x+r.position().x, r.size().y*r.size().x)), r.size());
}*/


/// \file dust.cc Automatic dust removal
#include "thread.h"
#include "file.h"
#include "interface.h"
#include "window.h"
#include "image.h"
#include "exif.h"
#include "jpeg.h"
#include "time.h"

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

struct DustRemover {
    ImageWidget view {Image()};
    Window window {&view, int2(1000, 750), "Dust Remover"_};

    DustRemover() {
        Time totalTime; totalTime.start();
        Folder folder("Pictures/Paper"_, home());
        Folder cache(".cache"_, folder, true);
        array<String> fileNames = folder.list(Files|Sorted);
        Time decodeTime;
        Time convertTime;
        Time sumTime;
        ImageF sum; // FIXME

        for(string fileName: fileNames) {
            Map file = Map(fileName, folder);
            if(imageFileFormat(file)!="JPEG"_) continue;
            if(parseExifTags(file).at("Exif.Photo.FNumber"_).real() != 6.3) continue;
            int2 imageSize (4000, 3000); //FIXME: = ::imageSize(file);
            if(sum) assert_(sum.size==imageSize); else sum = imageSize;

            // Cache conversion from sRGB JPEGs to raw (mmap'able) linear float images
            string baseName = section(fileName,'.');
            if(!existsFile(baseName, cache)) {
                decodeTime.start();
                log_(fileName);
                Image image = decodeImage(file);
                decodeTime.stop();
                if(image.size != imageSize) { log("Warning: inconsistent image size"); continue; }

                {convertTime.start();
                    log(" ->",baseName);
                    ImageTarget target (baseName, cache, image.size);
                    chunk_parallel(image.pixels.size, [&](uint, uint start, uint size) {
                        for(uint index: range(start, start+size)) {
                            byte4 sRGB = image.pixels[index];
                            float b = sRGB_reverse[sRGB.b];
                            float g = sRGB_reverse[sRGB.g];
                            float r = sRGB_reverse[sRGB.r];
                            target.pixels[index] = b+g+r; // Assumes dust affects all components equally
                        }
                    });
                    assert_(::sum(target.pixels));
                }convertTime.stop();
            }

            {sumTime.start(); // Sums intensities
                log(baseName);
                ImageSource image (baseName, cache, sum.size);

                chunk_parallel(image.pixels.size, [&](uint, uint start, uint size) {
                    for(uint index: range(start, start+size)) {
                        sum.pixels[index] += image.pixels[index];
                    }
                });
            }sumTime.stop();
        }
        Time viewTime; viewTime.start();
        view = sRGB(sum);
        viewTime.stop();
        log(sumTime, viewTime, totalTime);
    }
} application;
