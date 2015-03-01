#include "IT8.h"
#include "raw.h"
#include "demosaic.h"
#include "interface.h"
#include "window.h"
#include "png.h"

#if 1
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

#if 0
v4sf max(ref<v4sf> X) { v4sf y = float4(0); for(v4sf x: X) y = max(y, x); return y; }
void multiply(mref<v4sf> X, v4sf c) { for(v4sf& x: X) x *= c; }
Image4f normalize(Image4f&& image) { multiply(image, float4(1)/max(image)); return move(image); }

struct CalibrationVisualization {
    CalibrationVisualization() {
        for(string name: {"c0"_,"c1"_}) {
            writeFile(name+".png", encodePNG(convert(normalize(demosaic(ImageF(unsafeRef(cast<float>(Map(name))), CMV12000))))),
                      currentWorkingDirectory(), true);
       }
    }
} app;
#endif

#if 1
struct IT8Application : Application {
    string it8Charge = arguments()[0];
    string it8Image = arguments()[1];
    string name = section(it8Image,'.');

    mat4 rawRGBtoXYZ;
    map<String, Image> images;
    IT8Application() {
        Raw raw (it8Image);

        Map c0Map("c0"); ImageF c0 = ImageF(unsafeRef(cast<float>(c0Map)), Raw::size);
        Map c1Map("c1"); ImageF c1 = ImageF(unsafeRef(cast<float>(c0Map)), Raw::size);
        ImageF FPN (raw.size); // Corrected for fixed pattern noise
        real exposure = raw.exposure;
        real gain = (real) raw.gain / raw.gainDiv;
        log(it8Image,"\t",int(round(exposure*1e3)),"\t",raw.gain, raw.gainDiv);
        for(size_t i: range(FPN.Ref::size)) FPN[i] = raw[i] - gain * (c0[i] /*+ exposure * c1[i]*/);
        mat4 rawRGBtosRGB = mat4(sRGB);
        Image4f FPNRGB = demosaic(FPN);
        if(0) {
            IT8 it8(FPNRGB, readFile(it8Charge));
            rawRGBtoXYZ = it8.rawRGBtoXYZ;
            rawRGBtosRGB = mat4(sRGB) * rawRGBtoXYZ;
            images.insert(name+".chart", convert(mix(convert(it8.chart, rawRGBtosRGB), it8.spotsView)));
        }
        images.insert(name+".raw", convert(convert(demosaic(raw), rawRGBtosRGB)));
        images.insert(name+".FPN", convert(convert(FPNRGB, rawRGBtosRGB)));
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
        for(auto image: images) writeFile(image.key+".png", encodePNG(image.value), currentWorkingDirectory(), true);
    }
};
registerApplication(Export, export);
#endif
