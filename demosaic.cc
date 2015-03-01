#include "IT8.h"
#include "raw.h"
#include "demosaic.h"
#include "interface.h"
#include "window.h"
#include "png.h"

#if 0
struct List {
    List() {
        for(string file: currentWorkingDirectory().list(Files|Recursive)) {
            if(!endsWith(file, ".raw16")) continue;
            Raw raw(file, false);
            log(file,"\t", int(round(raw.exposure*1e3)),"\t", raw.gain, raw.gainDiv, raw.temperature);
        }
    }
} app;
#endif

generic void multiply(mref<T> Y, mref<T> X, T c) { for(size_t i: range(Y.size)) Y[i] = c * X[i]; }
ImageF normalize(ImageF&& target, const ImageF& source) { multiply(target, source, source.Ref::size/sum(source)); return move(target); }
ImageF normalize(const ImageF& source) { return normalize(source.size, source); }

#if 0
struct CalibrationVisualization {
    CalibrationVisualization() {
        for(string name: {"c0"_,"c1"_}) {
            writeFile(name+".png", encodePNG(convert(normalize(demosaic(ImageF(unsafeRef(cast<float>(Map(name))), CMV12000))))),
                      currentWorkingDirectory(), true);
       }
    }
} app;
#endif

Map c0Map("c0"); const ImageF c0 = ImageF(unsafeRef(cast<float>(c0Map)), Raw::size);
Map c1Map("c1"); const ImageF c1 = ImageF(unsafeRef(cast<float>(c1Map)), Raw::size);

/// Statically calibrated dark signal non uniformity correction
ImageF staticDSNU(ImageF&& target, const Raw& raw, float* DC=0) {
    real exposure = 0;// raw.exposure; // No effect
    real gain = (real) 1;//raw.gain;// * 3/*darkframe gainDiv*/ / raw.gainDiv;
    for(size_t i: range(target.Ref::size)) target[i] = raw[i] - gain * (c0[i] + exposure * c1[i]);
    if(DC) *DC = gain * (1 * ::sum(c0, 0.) + exposure * ::sum(c1, 0.)) / c0.Ref::size;
    return move(target);
}
ImageF staticDSNU(const Raw& raw, float* DC=0) { return staticDSNU(raw.size, raw, DC); }

/// Dynamic column wise dark signal non uniformity correction
ImageF dynamicDSNU(ImageF&& target, const ImageF& raw) {
    // Sums along columns
    real sum[2][raw.size.x];
    for(size_t dy: range(2)) mref<real>(sum[dy], raw.size.x).clear();
    for(size_t y: range(raw.size.y/2)) for(size_t dy: range(2)) for(size_t x: range(raw.size.x)) sum[dy][x] += raw(x, y*2+dy);
    int width = raw.size.x/2;
    float correction[2][raw.size.x];
    for(size_t dy: range(2)) { // Splits similar Bayer cells (FIXME: merge both greens)
        float profile[2][width];
        for(size_t x: range(width)) for(size_t dx: range(2)) profile[dx][x] = sum[dy][x*2+dx] / raw.size.y;
        for(size_t dx: range(2)) for(int x: range(width))
            correction[dy][x*2+dx] = profile[dx][x] - (profile[dx][abs(x-1)]+profile[dx][width-1-abs((width-1)-(x+1))])/2;
    }
#if 1
    // Substracts DSNU profile from all cells
    for(size_t y: range(raw.size.y/2)) for(size_t dy: range(2)) for(size_t x: range(raw.size.x)) target(x, y*2+dy) = raw(x, y*2+dy) - correction[dy][x];
#else
    // Substracts DSNU profile from selected cells
    for(size_t y: range(raw.size.y/2)) {
        for(size_t x: range(raw.size.x/2)) {
            target(x*2+0, y*2+0) = raw(x*2+0, y*2+0) - correction[0][x][0];
            target(x*2+1, y*2+0) = raw(x*2+1, y*2+0) - correction[0][x][1];
        }
        for(size_t x: range(raw.size.x/2)) {
            target(x*2+0, y*2+1) = raw(x*2+0, y*2+1) - correction[1][x][0];
            target(x*2+1, y*2+1) = raw(x*2+1, y*2+1) - correction[1][x][1];
        }
    }
#endif
    return move(target);
}
ImageF dynamicDSNU(const ImageF& raw) { return dynamicDSNU(raw.size, raw); }

ImageF transpose(ImageF&& target, const ImageF& source) {
    for(size_t y: range(source.size.y)) for(size_t x: range(source.size.x)) target(y, x) = source(x, y);
    return move(target);
}
ImageF transpose(const ImageF& source) { return transpose(int2(source.size.y, source.size.x), source); }

ImageF subtract(ImageF&& y, const ImageF& a, float b) {
    for(size_t i: range(y.Ref::size)) y[i] = a[i] - b;
    return move(y);
}
ImageF subtract(const ImageF& a, float b) { return subtract(a.size, a, b); }


ImageF subtract(ImageF&& y, const ImageF& a, const ImageF& b) {
    for(size_t i: range(y.Ref::size)) y[i] = a[i] - b[i];
    return move(y);
}
ImageF subtract(const ImageF& a, const ImageF& b) { return subtract(a.size, a, b); }


ImageF correlation(ImageF&& y, const ImageF& a, const ImageF& b) {
    assert_(a.Ref::size == y.Ref::size && b.Ref::size == y.Ref::size);
    assert_(a.size == y.size && b.size == y.size);
    float DCa = ::sum(a, 0.) / a.Ref::size;
    float DCb = ::sum(b, 0.) / b.Ref::size;
    real varA = 0; for(size_t i: range(a.Ref::size)) varA += sq(a[i]-DCa); varA /= a.Ref::size;
    real varB = 0; for(size_t i: range(b.Ref::size)) varB += sq(b[i]-DCb); varB /= b.Ref::size;
    float stdAB = sqrt(varA)*sqrt(varB);
    for(size_t i: range(y.Ref::size)) y[i] = (a[i]-DCa) * (b[i]-DCb) / stdAB;
    return move(y);
}
ImageF correlation(const ImageF& a, const ImageF& b) { return correlation(a.size, a, b); }

ImageF sqrtMultiply(ImageF&& y, const ImageF& a, const ImageF& b) {
    for(size_t i: range(y.Ref::size)) y[i] = sqrt(a[i] * b[i]);
    return move(y);
}
ImageF sqrtMultiply(const ImageF& a, const ImageF& b) { return sqrtMultiply(a.size, a, b); }

#if 1
struct IT8Application : Application {
    string it8Charge = arguments()[0];
    string it8Image = arguments()[1];
    string name = section(it8Image,'.');

    mat4 rawRGBtoXYZ;
    map<String, Image> images;
    IT8Application() {
        Raw raw (it8Image);

        // Fixed pattern noise correction
        float DC;
        ImageF unused staticDSNU = ::staticDSNU(raw, &DC);

        mat4 rawRGBtosRGB = mat4(sRGB);
        Image4f RGB = demosaic(subtract(raw, DC));
        if(1) {
            IT8 it8(RGB, readFile(it8Charge));
            rawRGBtoXYZ = it8.rawRGBtoXYZ;
            //log(rawRGBtoXYZ);
            rawRGBtosRGB = mat4(sRGB) * rawRGBtoXYZ;
            //images.insert(name+".chart", convert(mix(convert(it8.chart, rawRGBtosRGB), it8.spotsView)));
        }
        images.insert(name+".raw", convert(convert(demosaic(subtract(raw, DC)), rawRGBtosRGB)));
        //images.insert(name+".DSNU", convert(convert(demosaic(transpose(dynamicDSNU(transpose(dynamicDSNU(raw, 0)), 0))), rawRGBtosRGB)));
        //for(int i: {0,3}) images.insert(name+"."+str(i), convert(convert(demosaic(transpose(dynamicDSNU(transpose(dynamicDSNU(raw, i)), i))), rawRGBtosRGB)));
        //ImageF a = subtract(c0, DC);
        //images.insert(name+".c0", convert(convert(demosaic(a), rawRGBtosRGB)));
        const ImageF& a = staticDSNU;
        //images.insert(name+".static", convert(convert(demosaic(a), rawRGBtosRGB)));
        ImageF b = dynamicDSNU(subtract(raw, DC));
        //images.insert(name+".dynamic", convert(convert(demosaic(b), rawRGBtosRGB)));
        //images.insert(name+".correlation", convert(convert(demosaic(correlation(subtract(raw, a), subtract(raw, b))), rawRGBtosRGB)));
        images.insert(name+".hybrid", convert(convert(demosaic(subtract(raw, sqrtMultiply(subtract(raw, a), subtract(raw, b)))), rawRGBtosRGB)));
    }
};

struct WindowCycleView {
    buffer<ImageView> views;
    WidgetCycle layout;
    Window window {&layout, int2(1024, 768)};
    WindowCycleView(const map<String, Image>& images)
        : views(apply(images.size(), [&](size_t i) { return ImageView(share(images.values[i]), images.keys[i]); })),
          layout(toWidgets<ImageView>(views)) {}
};
struct Preview : IT8Application, WindowCycleView { Preview() : WindowCycleView(images) {} };
registerApplication(Preview);

struct Export : IT8Application {
    Export() {
        writeFile(name+".xyz", str(rawRGBtoXYZ), currentWorkingDirectory(), true);
        for(auto image: images) {
            log(image.key);
            writeFile(image.key+".png", encodePNG(image.value), currentWorkingDirectory(), true);
        }
    }
};
registerApplication(Export, export);
#endif
