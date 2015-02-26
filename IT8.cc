#include "data.h"
#include "color.h"
#include "interface.h"
#include "window.h"
#include "png.h"
#include "demosaic.h"

//struct WindowView : ImageView { Window window {this, int2(1024, 768)}; WindowView(Image&& image) : ImageView(move(image)) {} };
struct WindowCycleView : WidgetCycle {
    ImageView images[2];
    Window window {this, int2(1024, 768)};
    WindowCycleView(Image&& a, Image&& b) : WidgetCycle(toWidgets<ImageView>(images)), images{move(a), move(b)} {} };


Image4f parseIT8(ref<byte> it8) {
    TextData s(it8);
    Image4f target (24, 16);
    s.until("BEGIN_DATA\r\n");
    assert_(s);
    for(int i: range(12)) for(int j: range(1, 22 +1)) {
        s.skip(char('A'+i)+str(j));
        s.whileAny(' '); float x = s.decimal() / 100;
        s.whileAny(' '); float y = s.decimal() / 100;
        s.whileAny(' '); float z = s.decimal() / 100;
        s.until("\r\n");
        target(1+(j-1), 1+i) = {x,y,z,0};
    }
    for(int j: range(23 +1)) {
        s.skip("GS"+str(j));
        s.whileAny(' '); float x = s.decimal() / 100;
        s.whileAny(' '); float y = s.decimal() / 100;
        s.whileAny(' '); float z = s.decimal() / 100;
        s.until("\r\n");
        target(j, 1+12+1) = target(j, 1+12+1+1) = {x,y,z,0};
    }
    v4sf GS11 = target(11, 1+12+1);
    for(int j: range(24)) target(j, 0) = target(j, 1+12) = GS11;
    for(int i: range(13)) target(0, i) = target(23, i) = GS11;
    s.skip("END_DATA\r\n");
    assert_(!s);
    return target;
}

Image4f XYZtoBGR(Image4f&& source) {
    for(v4sf& value: source) {
        bgr3f bgr = XYZtoBGR(value[0], value[1], value[2]);
        value = {bgr[0], bgr[1], bgr[2], 0};
    }
    return move(source);
}

Image4f downsample(Image4f&& target, const Image4f& source) {
    assert_(target.size == source.size/2, target.size, source.size);
    for(uint y: range(target.height)) for(uint x: range(target.width))
        target(x,y) = (source(x*2+0,y*2+0) + source(x*2+1,y*2+0) + source(x*2+0,y*2+1) + source(x*2+1,y*2+1)) / float4(4);
    return move(target);
}
inline Image4f downsample(const Image4f& source) { return downsample(source.size/2, source); }

Image4f upsample(const Image4f& source) {
    Image4f target(source.size*2);
    for(uint y: range(source.height)) for(uint x: range(source.width)) {
        target(x*2+0,y*2+0) = target(x*2+1,y*2+0) = target(x*2+0,y*2+1) = target(x*2+1,y*2+1) = source(x,y);
    }
    return target;
}

// -- SSE --

static double SSE(const Image4f& A, const Image4f& B, int2 offset) {
    int2 size = B.size;
    double energy = 0;
    for(int y: range(size.y)) {
        for(int x: range(size.x)) {
            energy += sq3(A(x+offset.x, y+offset.y) - B(x, y))[0]; // SSE
        }
    }
    energy /= size.x*size.y;
    return energy;
}

int2 templateMatch(const Image4f& A, Image4f& b, int2& size) {
    array<Image4f> mipmap;
    mipmap.append(share(A));
    while(mipmap.last().size > b.size*3) mipmap.append(downsample(mipmap.last()));
    int2 bestOffset = mipmap.last().size / 2 - b.size / 2;
    const int searchWindowHalfLength = 16;
    for(const Image4f& a: mipmap.reverse()) {
        real bestSSE = inf;
        int2 levelBestOffset = bestOffset;
        for(int y: range(bestOffset.y-searchWindowHalfLength, bestOffset.y+searchWindowHalfLength +1)) {
            for(int x: range(bestOffset.x-searchWindowHalfLength, bestOffset.x+searchWindowHalfLength +1)) {
                float SSE = ::SSE(a, b, int2(x,y));
                if(SSE < bestSSE) {
                    bestSSE = SSE;
                    levelBestOffset = int2(x, y);
                }
            }
        }
        bestOffset = levelBestOffset;
        size = b.size;
        log(bestOffset, size, a.size);
        if(a.size == A.size) return bestOffset;
        b = upsample(b);
        bestOffset *= 2;
    }
    error("");
}

struct IT8 {
    string fileName = arguments()[1];
    string name = section(fileName,'.');
    Image target;
    Image b;
    IT8() {
        string it8charge = arguments()[0];
        Image4f it8bgr = XYZtoBGR(parseIT8(readFile(it8charge)));

        ImageF bayerCFA = fromRaw16(cast<uint16>(Map(fileName)), 4096, 3072);
        Image4f bgr = demosaic(bayerCFA);
        int2 size; int2 offset = templateMatch(bgr, it8bgr, size);
        target = convert(copy(cropShare(bgr, offset, size)));
        b = convert(it8bgr);
    }
};

struct Preview : IT8, WindowCycleView, Application { Preview() : WindowCycleView(share(target), share(b)) {} };
registerApplication(Preview);
struct Export : IT8, Application { Export() { writeFile(name+".png", encodePNG(target)); } };
registerApplication(Export, export);

