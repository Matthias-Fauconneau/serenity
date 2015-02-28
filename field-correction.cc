/// \file field-correction.cc Flat Field Correction
#include "raw.h"
#include "thread.h"
#include "map.h"

struct FlatFieldCorrection {
    FlatFieldCorrection() {
        Folder folder {"darkframes"};
        auto images = apply(folder.list(Files), [](string file) { return Raw(file); });
        ImageF c0 (Raw::size), c1 (Raw::size); // Affine fit dark energy = c0 + c1·exposureTime
        for(size_t pixelIndex: range(c0.Ref::size)) {
            // Direct evaluation of AtA and Atb
            real a00 = 0, a01 = 0, a11 = 0, b0 = 0, b1 = 0;
            for(const size_t constraintIndex: range(images.size)) {
                real x = images[constraintIndex].exposure; // c1 (·exposureTime)
                a00 += 1 * 1; // c0 (·1 constant)
                a01 += 1 * x;
                a11 += x * x;
                real b = images[constraintIndex][pixelIndex];
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
