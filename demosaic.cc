#include "interface.h"
#include "window.h"
#include "png.h"

struct WindowView : ImageView { Window window {this, int2(1024, 768)}; WindowView(Image&& image) : ImageView(move(image)) {} };

ImageF fromRaw16(ref<uint16> source, uint width, uint height) {
    ImageF target (width, height);
    assert_(target.Ref::size <= source.size, target.Ref::size, source.size, width, height);
    source = source.slice(0, width*height);
    uint16 max = ::max(source);
    for(size_t i: range(source.Ref::size)) target[i] = (float) source[i] / max;
    return target;
}

Image4f demosaic(const ImageF& source) {
    Image4f target (source.size);
    for(size_t y: range(source.height)) for(size_t x: range(source.width)) {
        int component = (int[]){1,2,1,0}[(y%2)*2+x%2]; // GRGB -> BGR
        v4sf bgr = float4(0);
        bgr[component] = source(x, y);
        target(x, y) = bgr;
    }
    return target;
}

struct Demosaic {
    string fileName = arguments()[0];
    string name = section(fileName,'.');
    ImageF source = fromRaw16(cast<uint16>(Map(fileName)), 4096, 3072);
    Image target = convert(demosaic(cropShare(source, source.size*int2(5, 8)/16, source.size/4)));
};

struct Preview : Demosaic, WindowView, Application { Preview() : WindowView(move(target)) {} };
registerApplication(Preview);
struct Export : Demosaic, Application { Export() { writeFile(name+".png", encodePNG(target)); } };
registerApplication(Export, export);
