#include "algebraic.h"
//include "conjugate.h"
#include "plot.h"
#include "window.h"
#include "layout.h"
#include "graphics.h"
#include "view.h"
#include "operators.h"

const Folder& folder = currentWorkingDirectory();
const uint N = fromInteger(arguments()[0]);

struct Application : Poll {
    // Evaluation
    Thread thread {19};
    const CLVolume projectionData;
    const CLVolume referenceVolume;
    int3 size = referenceVolume.size;
    int3 evaluationOrigin =  int3(0,0,size.z/4), evaluationSize = int3(size.xy(), size.z/2);
    const float SSQ = ::SSQ(referenceVolume, evaluationOrigin, evaluationSize);
    const string labels[2] {"SIRT"_, "CG"_};
    //unique<Reconstruction> reconstructions[2] {unique<Algebraic>(size, projectionData), unique<ConjugateGradient>(size, projectionData)};
    unique<Reconstruction> reconstructions[1] {unique<Algebraic>(size, projectionData)};

    // Interface
    int upsample = 256 / projectionData.size.x;

    Value sliceIndex = (referenceVolume.size.z-1) / 2;
    SliceView x {referenceVolume, upsample, sliceIndex};
    HList<SliceView> rSlices {apply(ref<unique<Reconstruction>>(reconstructions), [&](const Reconstruction& r){ return SliceView(r.x, upsample, sliceIndex);})};
    HBox slices {{&x, &rSlices}};

    Value projectionIndex = (projectionData.size.z-1) / 2;
    SliceView b {projectionData, upsample, projectionIndex};
    HList<VolumeView> rViews {apply(ref<unique<Reconstruction>>(reconstructions), [&](const Reconstruction& r){ return VolumeView(r.x, projectionData.size, upsample, projectionIndex);})};
    HBox views {{&b, &rViews}};

    Plot plot;

    VBox layout {{&slices, &views, &plot}};

    Window window {&layout, strx(projectionData.size)+" "_+strx(size)}; // FIXME

    Application() : Poll(0,0,thread), projectionData(int3(N,N,N), Map(strx(int3(N,N,N))+".proj"_, folder)),referenceVolume(int3(N,N,N), Map(strx(int3(N,N,N))+".ref"_, folder)) {
        queue(); thread.spawn();
        //window.actions[Space] = [this]{ queue(); };
    }
    void event() {
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        Reconstruction& r = reconstructions[index];
        r.step();
        const float SSE = ::SSE(referenceVolume, r.x, evaluationOrigin, evaluationSize);
        const float MSE = SSE / SSQ;
        const float PSNR = 10*log10( MSE );
        const uint k = r.k;
        log(str(labels[index]+"\t"_+str(k)+"\t"_+str(MSE)+"\t"_+str(-PSNR)));
        {Locker lock(window.renderLock);
            setWindow( &window );
            window.renderBackground(plot.target); plot[labels[index]].insertMulti(r.time/1000000000.f, -PSNR); plot.render();
            rSlices[index].render(); rViews[index].render();
            setWindow( 0 );
        }
        //cylinderCheck(r.x);
        queue();
    }
} app;
