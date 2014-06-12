#include "CG.h"
#include "SART.h"
#include "plot.h"
#include "window.h"
#include "layout.h"
#include "graphics.h"
#include "view.h"
#include "sum.h"

Folder folder = 1 ? "ellipsoids"_ : "rock"_;
const uint N = fromInteger(arguments()[0]);

struct Application : Poll {
    // Evaluation
    Thread thread {19};
    const CLVolume projectionData;
    const CLVolume referenceVolume;
    const float SSQ = ::SSQ(referenceVolume);
    SART
    //ConjugateGradient
    reconstruction {referenceVolume.size, projectionData};

    // Interface
    int upsample = 256 / projectionData.x;
    Value projectionIndex;
    SliceView b {projectionData, upsample, projectionIndex};
    VolumeView x {reconstruction.x, projectionData.size, upsample, projectionIndex};
#define DETAILS 1
#if DETAILS
    VolumeView oldX {reconstruction.oldX, projectionData.size, upsample, projectionIndex};
    SliceView Ax {reconstruction.Ax, upsample, projectionIndex};
    SliceView h {reconstruction.h, upsample, projectionIndex};
    VolumeView L {reconstruction.L, projectionData.size, upsample, projectionIndex};
    VolumeView LoAti {reconstruction.LoAti, projectionData.size, upsample, projectionIndex};
    HBox layout {{&b, &oldX, &Ax, &h, &L, &LoAti, &x}};
#else
    HBox top {{&b, &x}};
    Plot plot;
    Value sliceIndex;
    SliceView referenceSliceView {referenceVolume, upsample, sliceIndex};
    SliceView reconstructionSliceView {reconstruction.x, upsample, sliceIndex};
    HBox bottom {{&plot, &referenceSliceView, &reconstructionSliceView}};
    VBox layout {{&top, &bottom}};
#endif

    Window window {&layout, strx(projectionData.size)+" "_+strx(referenceVolume.size) /*, int2(3*projectionView.sizeHint().x,projectionView.sizeHint().y+512)*/}; // FIXME

    Application() : Poll(0,0,thread), projectionData(int3(N,N,N), Map(strx(int3(N,N,N))+".proj"_, folder)),referenceVolume(int3(N,N,N), Map(strx(int3(N,N,N))+".ref"_, folder)) {
        queue(); thread.spawn();
        window.actions[Space] = [this]{ queue(); };
    }
    void event() {
        reconstruction.step();
        const float SSE = ::SSE(referenceVolume, reconstruction.x);
        static float lastSSE = SSE;
        const float MSE = SSE / SSQ;
        const float PSNR = 10*log10( MSE );
        const uint k = reconstruction.k;
        log(k, MSE, -PSNR);
#if DETAILS
        oldX.render(); Ax.render(); h.render(); LoAti.render(); L.render();
#else
        window.renderBackground(plot.target); plot["CG"_].insertMulti(/*time*/ k, -PSNR); plot.render();
        reconstructionSliceView.render();
#endif
        x.render(); window.immediateUpdate();
        //if(SSE<=lastSSE)
            //queue();
        lastSSE = SSE;
    }
} app;
