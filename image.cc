#include "image.h"
#include "stream.h"
#include "vector.h"
#include "string.h"
#include <zlib.h>

#include "array.cc"
template class array<byte4>;

Image::Image(array<byte4>&& data, uint width, uint height):data((byte4*)data.data()),width(width),height(height),own(true) {
    assert(data.size() >= width*height, data.size(), width, height);
    assert(data.buffer.capacity);
    data.buffer.capacity = 0; //taking ownership
}

Image resize(const Image& image, uint width, uint height) {
    if(!image) return Image(16,16);
    if(width==image.width && height==image.height) return copy(image);
    Image target(width,height);
    const byte4* src = image.data;
    byte4* dst = target.data;
    if(image.width/width==image.height/height && !(image.width%width) && !(image.height%height)) { //integer box
        int scale = image.width/width;
        for(uint y=0; y<height; y++) {
            for(uint x=0; x<width; x++) {
                int4 s=zero; //TODO: alpha blending
                for(int i=0;i<scale;i++){
                    for(int j=0;j<scale;j++) {
                        s+= int4(src[i*image.width+j]);
                    }
                }
                *dst = byte4(s/(scale*scale));
                src+=scale, dst++;
            }
            src += (scale-1)*image.width;
        }
    } else { //nearest
        for(uint y=0; y<height; y++) {
            for(uint x=0; x<width; x++) {
                *dst = src[(y*height/image.height)*image.width+x*width/image.width];
                dst++;
            }
        }
    }
    return target;
}

Image swap(Image&& image) {
    uint32* p = (uint32*)image.data;
    for(uint i=0;i<image.width*image.height;i++) p[i] = swap32(p[i]);
    return move(image);
}

Image flip(Image&& image) {
    for(int y=0,h=image.height;y<h/2;y++) for(int x=0,w=image.width;x<w;x++) {
        swap(image(x,y),image(x,h-1-y));
    }
    return move(image);
}

declare(Image decodePNG(const array<byte>&),weak) { error("PNG support not linked"); }
declare(Image decodeJPEG(const array<byte>&),weak) { error("JPEG support not linked"); }
declare(Image decodeICO(const array<byte>&),weak) { error("ICO support not linked"); }

Image decodeImage(const array<byte>& file) {
    if(file.size()<4) return Image();
    if(startsWith(file,"\xFF\xD8"_)) return decodeJPEG(file);
    else if(startsWith(file,"\x89PNG"_)) return decodePNG(file);
    else if(startsWith(file,"\x00\x00\x01\x00"_)) return decodeICO(file);
    else { warn("Unknown image format",slice(file,0,4)); return Image(); }
}

#include "file.h"
#include "window.h"
struct ImageTest : Application {
    array<string> icons= split("www.phoronix.com/favicon.ico,www.pcinpact.com/favicon.ico,www.blender.org/favicon.ico,www.blendernation.com/favicon.ico,mango.blender.org/favicon.ico,planet.gentoo.org/favicon.ico,dot.kde.org/favicon.ico,planetKDE.org/favicon.ico,www.thedreamlandchronicles.com/favicon.ico,thedreamercomic.com/favicon.ico,www.questionablecontent.net/favicon.ico,wintersinlavelle.com/favicon.ico,www.redmoonrising.org/favicon.ico,waywardsons.keenspot.com/favicon.ico,www.girlgeniusonline.com/favicon.ico,www.sandraandwoo.com/favicon.ico,satwcomic.com/favicon.ico,www.gunnerkrigg.com/favicon.ico,www.misfile.com/favicon.ico,twokinds.keenspot.com/favicon.ico,www.egscomics.com/favicon.ico,www.terra-comic.com/favicon.ico,www.meekcomic.com/favicon.ico,www.kiwiblitz.com/favicon.ico,xkcd.com/favicon.ico,www.spindrift-comic.com/favicon.ico,www.sandraandwoo.com/favicon.ico,www.straysonline.com/favicon.ico,carciphona.com/favicon.ico,tryinghuman.com/favicon.ico,mail.google.com/favicon.ico"_,',');
    Grid<ImageView> grid=apply<ImageView>(icons, [](const string& file){ return decodeImage(readFile("/root/.cache/"_+file)); });
    Window window {&grid};
    ImageTest(array<string>&&){ window.localShortcut("Escape"_).connect(this, &Application::quit); window.show(); Window::sync(); }
};
Test(ImageTest)
