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
    Image4f target (source.size/2);
    for(size_t y: range(target.height)) for(size_t x: range(target.width)) {
        target(x, y) = {source(x*2+1, y*2+0), (source(x*2+0, y*2+0)+source(x*2+1, y*2+1))/2, source(x*2+0, y*2+1)};
    }
    return target;
}

struct Demosaic {
    string fileName = arguments()[0];
    string name = section(fileName,'.');
    ImageF source = fromRaw16(cast<uint16>(Map(fileName)), 4096, 3072);
    Image target = convert(demosaic(cropShare(source, source.size*int2(1, 3)/8, source.size/2)));
};

struct Preview : Demosaic, WindowView, Application { Preview() : WindowView(move(target)) {} };
registerApplication(Preview);
struct Export : Demosaic, Application { Export() { writeFile(name+".png", encodePNG(target)); } };
registerApplication(Export, export);
