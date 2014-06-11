#include "CG.h"
#include "plot.h"
#include "window.h"
#include "layout.h"
#include "graphics.h"
#include "view.h"
#include "sum.h"

struct Application : Poll {
    // Evaluation
    Thread thread {19};
    const CLVolume projectionData;
    const CLVolume referenceVolume;
    const float SSQ = ::SSQ(referenceVolume);
    ConjugateGradient reconstruction {referenceVolume.size, projectionData};

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

    Application(string projectionPath, string referencePath) : Poll(0,0,thread), projectionData(Map(projectionPath)),referenceVolume(Map(referencePath)) { queue(); thread.spawn(); }
    void event() {
        reconstruction.step();
        const float MSE = ::SSE(referenceVolume, reconstruction.x) / SSQ;
        const float PSNR = 10*log10( MSE );
        plot["CG"_].insert(reconstruction.totalTime/1000.f, -PSNR);
        log(reconstruction.k, reconstruction.totalTime/1000.f, MSE, -PSNR);
        reconstructionView.render(); reconstructionSliceView.render(); window.renderBackground(plot.target); plot.render(); window.immediateUpdate();
        queue();
    }
} app ("data/"_+arguments()[0]+".proj"_, "data/"_+arguments()[0]+".ref"_);
