#include "cdf.h"
//include "SIRT.h"
//#include "approximate.h"
#include "adjoint.h"
//include "MLEM.h"
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

struct Tomography : Poll {
    VolumeCDF projectionData {Folder("Preprocessed"_/*LIN_PSS"_*/, Folder("Data"_, home()))};

    const int downsampleFactor = 4;
    int3 reconstructionSize = int3(512,512,896) / downsampleFactor;

    array<ImageF> images;
    array<Projection> projections;

    unique<Reconstruction> reconstructions[1] {unique<Adjoint>(reconstructionSize, true, true)};

    Thread thread;

    Tomography() : Poll(0,0,thread) {
        const uint stride = projectionData.volume.sampleCount.z / (896/downsampleFactor);
        for(int index=0; index<projectionData.volume.sampleCount.z; index+=stride) {
            images << downsample(slice(projectionData.volume, index));
            projections << Projection(reconstructionSize, images.last().size(), index);
        }
        log(reconstructionSize, projections.size, images.first().size());  // 2: (256, 256, 448) 230 (252, 189), 4:
        for(auto& reconstruction: reconstructions) reconstruction->initialize(projections, images);
        queue();
        thread.spawn();
    }

    void event() {
        // Step forward the reconstruction which consumed the least time.
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        //log_(left(labels[index],16)+"\t"_);
        reconstructions[index]->step(projections, images);
#if PLOT
        const float PSNR = 10*log10(SSQ / ::SSE(reconstructions[index]->x, x0));
        plot[labels[index]].insert(reconstructions[index]->totalTime.toFloat(), PSNR);
#endif
        //log("\t", PSNR);
        queue();
    }
};

struct Application : Tomography {
#if WINDOW
    //string labels[1] = {"Adjoint"_};
    HList<View> views{{{projectionData.volume, projections, false},{reconstructions[0]->x, projections}}};
#if PLOT
    Plot plot;
    HBox layout {{&views, &plot}};
#else
    VBox layout {{&views}};
#endif
    Window window {&layout, "Reconstruction"_};
#endif
} app;
