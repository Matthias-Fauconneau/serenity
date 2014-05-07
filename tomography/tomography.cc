#include "phantom.h"
#include "update.h"
#include "adjoint.h"
#include "approximate.h"
#include "MLEM.h"
#include "plot.h"
#include "window.h"
#include "layout.h"
#include "graphics.h"
#include "view.h"

inline float SSQ(const VolumeF& x) {
    const float* xData = x;
    float SSQ[coreCount] = {};
    chunk_parallel(x.size(), [&](uint id, uint offset, uint size) {
        float accumulator = 0;
        for(uint i: range(offset,offset+size)) accumulator += sq(xData[i]);
        SSQ[id] += accumulator;
    });
    return sum(SSQ);
}

inline float SSE(const VolumeF& a, const VolumeF& b) {
    assert_(a.size() == b.size());
    const float* aData = a; const float* bData = b;
    float SSE[coreCount] = {};
    chunk_parallel(a.size(), [&](uint id, uint offset, uint size) {
        float accumulator = 0;
        for(uint i: range(offset,offset+size)) accumulator += sq(aData[i] - bData[i]);
        SSE[id] += accumulator;
    });
    return sum(SSE);
}



struct Tomography {
#if DEBUG
    const uint N = 64;
#else
    const uint N = 128;
#endif
    const uint P = N; // * threadCount; // Exact adjoint method (gather, scatter) has same space requirement as approximate adjoint method (gather, gather) when P ~ TN
    Phantom phantom {16};
    VolumeF x0 = phantom.volume(N);
    buffer<Projection> projections {P};
    buffer<ImageF> images {P};
#if 1 // Single
#if 0 // Adjoint
    string labels[1] = {"Adjoint"_};
    unique<Reconstruction> reconstructions[1] {unique<Adjoint>(N)};
#elif 0 // Bilinear
    string labels[1] = {"Bilinear"_};
    unique<Reconstruction> reconstructions[1] {unique<Approximate>(N)};
#else // MLEM
    string labels[1] = {"MLEM"_};
    unique<Reconstruction> reconstructions[1] {unique<MLEM>(N)};
#endif
    HList<View> views{{{0, &reconstructions[0]->x}}};
#elif 0 // Comparison
#if 0 // Adjoint: Direct vs Filtered
    string labels[2] = {"Direct"_,"Filtered"_};
    unique<Reconstruction> reconstructions[2] {unique<Adjoint>(N), unique<Adjoint>(N, true)};
#elif 1 // Bilinear: Direct vs Filtered
    string labels[2] = {"Direct"_,"Filtered"_};
    unique<Reconstruction> reconstructions[2] {unique<Approximate>(N), unique<Approximate>(N, true)};
#elif 0 // Direct: Adjoint vs Bilinear
    string labels[2] = {"Adjoint"_,"Bilinear"_};
    unique<Reconstruction> reconstructions[2] {unique<Adjoint>(N), unique<Approximate>(N)};
#elif 1 // Filtered: Adjoint vs Bilinear
    string labels[2] = {"Adjoint"_,"Bilinear"_};
    unique<Reconstruction> reconstructions[2] {unique<Adjoint>(N, true), unique<Approximate>(N, true)};
#endif
    HList<View> views{{{0, &reconstructions[0]->x},{0, &x0},{0, &reconstructions[1]->x}}};
#elif 0 //Direct/Filtered Adjoint/Bilinear
    string labels[4] = {"Direct Adjoint"_,"Direct Bilinear"_,"Ramp Adjoint"_,"Ramp Bilinear"_};
    unique<Reconstruction> reconstructions[4] {unique<Adjoint>(N, false), unique<Approximate>(N, false), unique<Adjoint>(N, true), unique<Approximate>(N, true)};
    UniformGrid<View> views{{{0, &reconstructions[0]->x},{0, &reconstructions[1]->x},{0, &reconstructions[2]->x},{0, &reconstructions[3]->x}}};
#else // [Filtered] [Regularized] [Adjoint]
    string labels[8] = {"Adjoint"_,"Filtered Adjoint"_,"Regularized Adjoint"_,"Filtered Regularized Adjoint"_, "Bilinear"_,"Filtered Bilinear"_,"Regularized Bilinear"_,"Filtered Regularized Bilinear"_ };
    unique<Reconstruction> reconstructions[8] {unique<Adjoint>(N, false, false), unique<Adjoint>(N, true, false), unique<Adjoint>(N, true, false), unique<Adjoint>(N, true, true),
                unique<Approximate>(N, false, false), unique<Approximate>(N, true, false), unique<Approximate>(N, true, false), unique<Approximate>(N, true, true)
                                              };
    UniformGrid<View> views{{{0, &reconstructions[0]->x},{0, &reconstructions[1]->x},{0, &reconstructions[2]->x},{0, &reconstructions[3]->x},
            {0, &reconstructions[4]->x},{0, &reconstructions[5]->x},{0, &reconstructions[6]->x},{0, &reconstructions[7]->x}}, 4};
#endif
    float SSQ = ::SSQ(x0);
#define WINDOW 1
#if WINDOW
#define PLOT 1
#if PLOT
    Plot plot;
    HBox layout {{&views, &plot}};
#else
    VBox layout {{&views}};
#endif
    Window window {&layout, int2(3*512,2*512), "Tomography"_};
#endif
    //~Music() { writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"ondemand"_); }
    Tomography() {
        //execute("/bin/sh"_, /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"performance"_);

        // Projects phantom
        for(uint i: range(projections.size)) {
            mat4 projection = mat4().rotateX(-PI/2 /*Pitch*/).rotateZ(2*PI*i/N /*Yaw*/);
            ImageF image = float((N-1)/2) * phantom.project(N, projection.scale(vec3(N-1)/2.f));
            projections[i] = Projection(projection, image.size());
            images[i] = move(image);
        }

        for(auto& reconstruction: reconstructions) reconstruction->initialize(projections, images);
        //view.volume = &((Adjoint*)reconstructions[0].pointer)->p;
        step();

#if WINDOW
        /*window.actions[Escape] = []{ exit(); };
        window.background = Window::NoBackground;
        window.show();*/
        window.displayed = {this, &Tomography::step};
#else
        for(uint unused i: range(3)) step();
#endif
    }

    void step() {
        // Step forward the reconstruction which consumed the least time.
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        //log_(left(labels[index],16)+"\t"_);
        reconstructions[index]->step(projections, images);
        const float PSNR = 10*log10(SSQ / ::SSE(reconstructions[index]->x, x0));
#if PLOT
        plot[labels[index]].insert(reconstructions[index]->totalTime.toFloat(), PSNR);
#endif
        //log("\t", PSNR);
#if WINDOW
        if(window.target) { // FIXME
#if PLOT
            // Renders plot
            {Rect rect = layout.layout(window.size)[1];
                Image target = clip(window.target, rect);
                fill(target, Rect(target.size()), white);
                plot.render(target);
            }
#endif
            // Renders only the reconstruction which updated
            uint viewIndex = index;// ? 2 : 0;
            Rect viewsRect = layout.layout(window.size)[0];
            Rect rect = viewsRect.position() + views.layout(viewsRect.size())[viewIndex];
            Image target = clip(window.target, rect);
            views[viewIndex].render(target);
            window.putImage();
        }
#endif
    }
} tomography;
