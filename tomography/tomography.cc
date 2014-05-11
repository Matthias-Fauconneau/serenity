#include "cdf.h"
#include "adjoint.h"
//#include "approximate.h"
//#include "MLEM.h"
//#include "adjoint.h"
#include "SIRT.h"
#include "plot.h"
#include "window.h"
#include "layout.h"
#include "graphics.h"
#include "view.h"

#define WINDOW 1
#define PLOT 0

#if PLOT
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
#endif

struct Tomography {
    int3 reconstructionSize = int3(512,512,896);
    VolumeCDF projectionData {Folder("Preprocessed"_/*LIN_PSS"_*/, Folder("Data"_, home()))};
    array<Projection> projections;
    array<ImageF> images;
    //string labels[1] = {"Adjoint"_};
    unique<Reconstruction> reconstructions[1] {unique<SIRT>(reconstructionSize)};
    HList<View> views{{&reconstructions[0]->x}};


#if WINDOW
#if PLOT
    Plot plot;
    HBox layout {{&views, &plot}};
#else
    VBox layout {{&views}};
#endif
    Window window {&layout, "Reconstruction"_};
#endif
    Tomography() {
        const uint stride = 8; // Use 1/8 of projections
        for(int index=0; index<projectionData.volume.sampleCount.z; index+=stride) {
            images << slice(projectionData.volume, index);
            projections << Projection(reconstructionSize, images.last().size(), index);
        }

        for(auto& reconstruction: reconstructions) reconstruction->initialize(projections, images);
        step();

#if WINDOW
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
#if PLOT
        const float PSNR = 10*log10(SSQ / ::SSE(reconstructions[index]->x, x0));
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
            uint viewIndex = index;
            Rect viewsRect = layout.layout(window.size)[0];
            Rect rect = viewsRect.position() + views.layout(viewsRect.size())[viewIndex];
            Image target = clip(window.target, rect);
            views[viewIndex].render(target);
            window.putImage();
        }
#endif
    }
} tomography;
