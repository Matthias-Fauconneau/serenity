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
    WindowCycleView(const map<String, Image>& images)
        : views(apply(images.size(), [&](size_t i) { return ImageView(share(images.values[i]), images.keys[i]); })),
          layout(toWidgets<ImageView>(views)) {}
};

v4sf max(ref<v4sf> X) { v4sf y = float4(0); for(v4sf x: X) y = max(y, x); return y; }
void multiply(mref<v4sf> X, v4sf c) { for(v4sf& x: X) x *= c; }
Image4f normalize(Image4f&& image) { multiply(image, float4(1)/max(image)); return move(image); }

struct FlatFieldCorrection {
    Folder folder {"darkframes"};
    map<String, Image> images;
    FlatFieldCorrection() {
        auto maps = apply(folder.list(Files), [this](string fileName) { return Map(fileName, folder); });
        int2 size (4096, 3072);
        map<real, ImageF> images;
        for(const Map& map: maps) {
            ref<uint16> file = cast<uint16>(map);

            ref<uint16> registers = file.slice(file.size-128);
            enum { ExposureTime=71 /*71-72*/, BitMode = 118 };
            int bits = ((int[]){12, 10, 8, 0})[registers[BitMode]&0b11];
            int fot_overlap = (34 * (registers[82] & 0xFF)) + 1;
            real exposure = (((registers[ExposureTime+1] << 16) + registers[ExposureTime] - 1)*(registers[85] + 1) + fot_overlap) * bits / 300e6;

            ImageF image = fromRaw16(file.slice(0, file.size-128), size);

            images.insert(exposure, move(image));
        }
        ImageF c0 (size), c1 (size); // Affine fit dark energy = c0 + c1·exposureTime
        for(size_t pixelIndex: range(c0.Ref::size)) {
            // Direct evaluation of AtA and Atb
            real a00 = 0, a01 = 0, a11 = 0, b0 = 0, b1 = 0;
            for(const size_t constraintIndex: range(images.size())) {
                real x = images.keys[constraintIndex]; // c1 (·exposureTime)
                a00 += 1 * 1; // c0 (·1 constant)
                a01 += 1 * x;
                a11 += x * x;
                real b = images.values[constraintIndex][pixelIndex];
                b0 += 1 * b;
                b1 += x * b;
            }
            // Solves AtA x = Atb
            float det = a00*a11-a01*a01; // |AtA| (a10=a01)
            assert_(det);
            c0[pixelIndex] = (b0*a11 - a01*b1) / det;
            c1[pixelIndex] = (a00*b1 - b0*a01) / det;
        }
        this->images.insert("c0"__, move(convert(normalize(demosaic(c0)))));
        this->images.insert("c1"__, move(convert(normalize(demosaic(c1)))));
    }
} app;

struct Preview : FlatFieldCorrection, WindowCycleView, Application { Preview() : WindowCycleView(images) {} };
registerApplication(Preview);
