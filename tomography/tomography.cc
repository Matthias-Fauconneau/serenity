#include "CG.h"
#include "SART.h"
#include "plot.h"
#include "window.h"
#include "layout.h"
#include "graphics.h"
#include "view.h"
#include "sum.h"

Folder folder = 1 ? "ellipsoids"_ : "rock"_;
const string name = arguments()[0];

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
    int upsample = 4;
    Value projectionIndex;
    SliceView projectionView {projectionData, upsample, projectionIndex};
    VolumeView reconstructionView {reconstruction.x, projectionData.size, upsample, projectionIndex};
    HBox top {{&projectionView, &reconstructionView}};
    Plot plot;
    Value sliceIndex;
    SliceView referenceSliceView {referenceVolume, upsample, sliceIndex};
    SliceView reconstructionSliceView {reconstruction.x, upsample, sliceIndex};
    HBox bottom {{&plot, &referenceSliceView, &reconstructionSliceView}};
    VBox layout {{&top, &bottom}};
    Window window {&layout, strx(projectionData.size)+" "_+strx(referenceVolume.size) , int2(3*projectionView.sizeHint().x,projectionView.sizeHint().y+512)}; // FIXME

    Application() : Poll(0,0,thread), projectionData(Map(name+".proj"_, folder)),referenceVolume(Map(name+".ref"_, folder)) { queue(); thread.spawn(); }
    void event() {
        reconstruction.step();
        const float SSE = ::SSE(referenceVolume, reconstruction.x);
        static float lastSSE = SSE;
        const float MSE = SSE / SSQ;
        const float PSNR = 10*log10( MSE );
        const uint k = reconstruction.k;
        //const float time = reconstruction.totalTime/1000000000.f; //FIXME: OpenCL kernel time
        plot["CG"_].insertMulti(/*time*/ k, -PSNR);
        log(k, /*reconstruction.totalTime/1000000000.f,*/ MSE, -PSNR);
        reconstructionView.render(); reconstructionSliceView.render(); window.renderBackground(plot.target); plot.render(); window.immediateUpdate();
        //if(SSE<=lastSSE)
            queue();
        lastSSE = SSE;
    }
} app;
