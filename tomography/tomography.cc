#include "algebraic.h"
#include "conjugate.h"
#include "plot.h"
#include "window.h"
#include "layout.h"
#include "graphics.h"
#include "view.h"
#include "sum.h"

Folder folder = 0 ? "ellipsoids"_ : "rock"_;
const uint N = fromInteger(arguments()[0]);

struct Application : Poll {
    // Evaluation
    Thread thread {19};
    const CLVolume projectionData;
    const CLVolume referenceVolume;
    const float SSQ = ::SSQ(referenceVolume);
    const string labels[2] {"SIRT"_, "CG"_};
    unique<Reconstruction> reconstructions[2] {unique<Algebraic>(referenceVolume.size, projectionData), unique<ConjugateGradient>(referenceVolume.size, projectionData)};

    // Interface
    int upsample = 256 / projectionData.size.x;

    SliceView x {referenceVolume, upsample, projectionIndex};
    HList<SliceView> rSlices {apply(ref<unique<Reconstruction>>(reconstructions), [&](const Reconstruction& r){ return SliceView(r.x, upsample, projectionIndex);})};
    HBox slices {{&x, &rSlices}};

    Value projectionIndex;
    SliceView b {projectionData, upsample, projectionIndex};
    HList<VolumeView> rViews {apply(ref<unique<Reconstruction>>(reconstructions), [&](const Reconstruction& r){ return VolumeView(r.x, projectionData.size, upsample, projectionIndex);})};
    HBox views {{&b, &rViews}};

    Plot plot;

    VBox layout {{&slices, &views, &plot}};

    Window window {&layout, strx(projectionData.size)+" "_+strx(referenceVolume.size)}; // FIXME

    Application() : Poll(0,0,thread), projectionData(int3(N,N,N), Map(strx(int3(N,N,N))+".proj"_, folder)),referenceVolume(int3(N,N,N), Map(strx(int3(N,N,N))+".ref"_, folder)) {
        queue(); thread.spawn();
        //window.actions[Space] = [this]{ queue(); };
    }
    void event() {
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        Reconstruction& r = reconstructions[index];
        r.step();
        const float SSE = ::SSE(referenceVolume, r.x);
        const float MSE = SSE / SSQ;
        const float PSNR = 10*log10( MSE );
        const uint k = r.k;
        log(k, MSE, -PSNR);
        {Locker lock(window.renderLock);
            window.renderBackground(plot.target); plot[labels[index]].insertMulti(/*time*/ k, -PSNR); plot.render();
            rSlices[index].render(); rViews[index].render();
            window.immediateUpdate();
        }
        queue();
    }
} app;
