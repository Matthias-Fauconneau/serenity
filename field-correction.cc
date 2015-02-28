/// \file field-correction.cc Flat Field Correction
#include "raw.h"
#include "thread.h"
#include "map.h"

struct FlatFieldCorrection {
    FlatFieldCorrection() {
        Folder folder {"darkframes"};
        auto maps = apply(folder.list(Files), [&](string fileName) { return Map(fileName, folder); });
        map<real, ImageF> images;
        for(const Map& map: maps) {
            ref<uint16> file = cast<uint16>(map);
            ImageF image = fromRaw16(file);
            images.insert(exposure(file), move(image));
        }
        int2 size = images.values[0].size;
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
        writeFile("c0", cast<byte>(c0), currentWorkingDirectory(), true);
        writeFile("c1", cast<byte>(c1), currentWorkingDirectory(), true);
    }
} app;
