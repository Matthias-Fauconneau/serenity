/// \file field-correction.cc Flat Field Correction
#include "data.h"
#include "interface.h"
#include "window.h"
#include "png.h"
#include "demosaic.h"
#include "algebra.h"
#include "matrix.h"
#include "IT8.h"

struct WindowCycleView {
    buffer<ImageView> views;
    WidgetCycle layout;
    Window window {&layout, int2(1024, 768)};
    WindowCycleView(ref<Image> images, ref<String> captions={})
        : views(apply(images.size, [=](size_t i) { return ImageView(share(images[i]), captions[i]); })), layout(toWidgets<ImageView>(views)) {}
};

v4sf max(ref<v4sf> X) { v4sf y = float4(0); for(v4sf x: X) y = max(y, x); return y; }
void multiply(mref<v4sf> X, v4sf c) { for(v4sf& x: X) x *= c; }

struct FlatFieldCorrection {
    Folder folder {"darkframes"};
    array<Image> images;
    array<String> captions;
    FlatFieldCorrection() {
        mat4 rawRGBtosRGB = mat4(sRGB);
        if(0) {
            string it8ChargeFileName = arguments()[0];
            string it8ImageFileName = arguments()[1];
            Map map(it8ImageFileName);
            ref<uint16> it8Image = cast<uint16>(map);
            mat4 rawRGBtoXYZ = fromIT8(it8Image, readFile(it8ChargeFileName));
            rawRGBtosRGB = mat4(sRGB) * rawRGBtoXYZ;
            images.append(convert(convert(demosaic(fromRaw16(it8Image.slice(0, it8Image.size-128), 4096, 3072)), rawRGBtosRGB)));
            captions.append(copyRef(it8ImageFileName));
        }

        auto maps = apply(folder.list(Files), [this](string fileName) { return Map(fileName, folder); });
        map<real, ImageF> images;
        for(const Map& map: maps) {
            ref<uint16> file = cast<uint16>(map);

            ref<uint16> registers = file.slice(file.size-128);
            enum { ExposureTime=71 /*71-72*/, BitMode = 118 };
            int bits = ((int[]){12, 10, 8, 0})[registers[BitMode]&0b11];
            int fot_overlap = (34 * (registers[82] & 0xFF)) + 1;
            real exposure = (((registers[ExposureTime+1] << 16) + registers[ExposureTime] - 1)*(registers[85] + 1) + fot_overlap) * bits / 300e6;

            ImageF image = fromRaw16(file.slice(0, file.size-128), 4096, 3072);

            images.insert(exposure, move(image));
        }

        /*Image4f image_sRGB = convert(demosaic(image), rawRGBtosRGB);
        images.append(convert(image_sRGB));
        captions.append(copyRef(fileName));*/
    }
};

struct Preview : FlatFieldCorrection, WindowCycleView, Application { Preview() : WindowCycleView(images, captions) {} };
registerApplication(Preview);
