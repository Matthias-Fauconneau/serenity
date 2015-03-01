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

v4sf max(ref<v4sf> X) { v4sf y = float4(0); for(v4sf x: X) y = max(y, x); return y; }
void multiply(mref<v4sf> X, v4sf c) { for(v4sf& x: X) x *= c; }
Image4f normalize(Image4f&& image) { multiply(image, float4(1)/max(image)); return move(image); }

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

Map c0Map("c0"); ImageF c0 = ImageF(unsafeRef(cast<float>(c0Map)), Raw::size);
Map c1Map("c1"); ImageF c1 = ImageF(unsafeRef(cast<float>(c1Map)), Raw::size);

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
ImageF dynamicDSNU(ImageF&& target, const ImageF& raw, const float sigma = 0) {
    // Sums along columns
    real sum[2][raw.size.x]; for(size_t dy: range(2)) mref<real>(sum[dy], raw.size.x).clear();
    for(size_t y: range(raw.size.y/2)) for(size_t dy: range(2)) for(size_t x: range(raw.size.x)) sum[dy][x] += raw(x, y*2+dy);
    float correction[2][raw.size.x];
    for(size_t dy: range(2)) { // Splits similar Bayer cells (FIXME: merge both greens)
        int width = raw.size.x/2;
        float profile[2][width];
        for(size_t x: range(width)) for(size_t dx: range(2)) profile[dx][x] = sum[dy][x*2+dx] / raw.size.y;

        if(sigma == 0) { // Laplacian high pass
            for(size_t i: range(2)) for(int x: range(width))
                correction[dy][x*2+i] = profile[i][x] - (profile[i][abs(x-1)]+profile[i][width-1-abs((width-1)-(x+1))])/2;
        } else { // Dirac-Gaussian high pass
            // Kernel (unsharp mask)
            int radius = ceil(3*sigma);
            size_t N = radius+1+radius;
            float kernel[N];
            for(int dx: range(N)) kernel[dx] = -gaussian(sigma, dx-radius); // Sampled gaussian kernel (FIXME)
            float scale = -1/::sum(ref<float>(kernel,N), 0.);
            for(float& w: kernel) w *= scale; // Normalizes
            kernel[radius] += 1; // Dirac

            // Convolves profile with kernel (mirror boundary conditions)
            for(size_t i: range(2)) for(int x: range(width)) {
                float sum = 0;
                for(int dx: range(N)) sum += kernel[dx] * profile[i][width-1-abs(abs((width-1)-x-radius+dx))];
                //assert_(correction[x*2+i]>0);
                correction[dy][x*2+i] = sum;
            }
        }
    }
    // Substracts DSNU profile from columns
    for(size_t y: range(raw.size.y/2)) for(size_t dy: range(2)) for(size_t x: range(raw.size.x)) target(x, y*2+dy) = raw(x, y*2+dy) - correction[dy][x];
    return move(target);
}
ImageF dynamicDSNU(const ImageF& raw, const float sigma = 0) { return dynamicDSNU(raw.size, raw, sigma); }

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
        for(size_t i: range(raw.Ref::size)) raw[i] -= DC;

        mat4 rawRGBtosRGB = mat4(sRGB);
        Image4f RGB = demosaic(raw);
        if(1) {
            IT8 it8(RGB, readFile(it8Charge));
            rawRGBtoXYZ = it8.rawRGBtoXYZ;
            //log(rawRGBtoXYZ);
            rawRGBtosRGB = mat4(sRGB) * rawRGBtoXYZ;
            //images.insert(name+".chart", convert(mix(convert(it8.chart, rawRGBtosRGB), it8.spotsView)));
        }
        //images.insert(name+".raw", convert(convert(demosaic(raw), rawRGBtosRGB)));
        //images.insert(name+".DSNU", convert(convert(demosaic(transpose(dynamicDSNU(transpose(dynamicDSNU(raw, 0)), 0))), rawRGBtosRGB)));
        //for(int i: {0,3}) images.insert(name+"."+str(i), convert(convert(demosaic(transpose(dynamicDSNU(transpose(dynamicDSNU(raw, i)), i))), rawRGBtosRGB)));
        images.insert(name+".c0", convert(convert(normalize(demosaic(subtract(c0, DC))), rawRGBtosRGB)));
        //images.insert(name+".static", convert(convert(normalize(demosaic(subtract(raw, staticDSNU))), rawRGBtosRGB)));
        //images.insert(name+".dynamic", convert(convert(normalize(demosaic(subtract(raw, transpose(dynamicDSNU(transpose(dynamicDSNU(raw, 0)), 0))))), rawRGBtosRGB)));
        images.insert(name+".dynamic", convert(convert(normalize(demosaic(subtract(raw, dynamicDSNU(raw, 0)))), rawRGBtosRGB)));
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
