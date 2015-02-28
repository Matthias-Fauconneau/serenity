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
    //array<Image> images;
    //array<String> captions;
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
            const size_t unknownCount = 2, constraintCount = images.size();
            Matrix A (constraintCount, unknownCount); Vector b (constraintCount);

            for(const size_t constraintIndex: range(constraintCount)) { // Each image defines a constraint for linear least square regression
                A(constraintIndex, 0) = 1; // c0 (·1 constant)
                A(constraintIndex, 1) = images.keys[constraintIndex]; // c1 (·exposureTime)
                b[constraintIndex] = images.values[constraintIndex][pixelIndex]; // XYZ
            }

            // -- Solves linear least square system
            Matrix At = transpose(A);
            Matrix AtA = At * A;
            Vector Atb = At * b;
            Vector x = solve(move(AtA),  Atb); // Solves AtA = Atb
            //Vector r = A*x - b; log(r); for(float v: r) if(isNaN(v)) error("No solution found");
            // Inserts solution
            c0[pixelIndex] = x[0];
            c1[pixelIndex] = x[1];
        }
    }
} app;

//struct Preview : FlatFieldCorrection, WindowCycleView, Application { Preview() : WindowCycleView(images, captions) {} };
//registerApplication(Preview);
