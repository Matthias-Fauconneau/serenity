#include "algebraic.h"
#include "conjugate.h"
#include "MLTR.h"

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
#define COMPARE 0
#if COMPARE
    const string labels[3] {"SIRT"_, "CG"_,"MLTR"_};
    unique<Reconstruction> reconstructions[2] {unique<Algebraic>(size, projectionData), unique<ConjugateGradient>(size, projectionData)/*, unique<MLTR>(size, projectionData)*/};
#else
    const string labels[1] {""_};
    ::MLTR MLTR {size, projectionData};
#endif

    // Interface
    int upsample = 256 / projectionData.size.x;

    Value sliceIndex = (referenceVolume.size.z-1) / 2;
    Value projectionIndex = (projectionData.size.z-1) / 2;
    Value subsetIndex = (MLTR.subsetSize-1) / 2;

    SliceView x {referenceVolume, upsample, sliceIndex};
#if COMPARE
    HList<SliceView> rSlices {apply(ref<unique<Reconstruction>>(reconstructions), [&](const Reconstruction& r){ return SliceView(r.x, upsample, sliceIndex);})};
#else
    SliceView sliceViews[7] {{MLTR.Ax, upsample, subsetIndex,"Ax"_}, {MLTR.r, upsample, subsetIndex,"r"_}, {MLTR.Atr, upsample, sliceIndex,"Atr"_}, {MLTR.w, upsample, subsetIndex,"w"_}, {MLTR.Atw, upsample, sliceIndex,"Atw"_}, {MLTR.AtrAtw, upsample, sliceIndex,"Atr / Atw"_}, {MLTR.x, upsample, sliceIndex,"x"_}};
    //HList<SliceView> rSlices {buffer<SliceView>(ref<SliceView>(sliceViews,7))};
    //HTuple<SliceView> rSlices {{MLTR.Ax, upsample, subsetIndex,"Ax"_}};
    HBox rSlices{ apply(mref<SliceView>(sliceViews), [](SliceView& o)->Widget*{return &o;}) };
#endif
    HBox slices {{&x, &rSlices}};

    SliceView b {projectionData, upsample, projectionIndex};
#if COMPARE
    HList<VolumeView> rViews {apply(ref<unique<Reconstruction>>(reconstructions), [&](const Reconstruction& r){ return VolumeView(r.x, projectionData.size, upsample, projectionIndex);})};
#else
    VolumeView volumeViews [3] {{MLTR.Atr, projectionData.size, upsample, sliceIndex, "Atr"_}, {MLTR.Atw, projectionData.size, upsample, sliceIndex, "Atw"_}, {MLTR.x, projectionData.size, upsample, sliceIndex, "x"_}};
    HList<VolumeView> rViews {volumeViews};
#endif
    HBox views {{&b, &rViews}};

    Plot plot;

    VBox layout {{&slices, &views, &plot}};

    Window window {&layout, strx(projectionData.size)+" "_+strx(size)}; // FIXME

    Application() : Poll(0,0,thread),
        projectionData(int3(N,N,N), Map(strx(int3(N,N,N))+".proj"_, folder)),
        referenceVolume(int3(N,N,N), Map(strx(int3(N,N,N))+".ref"_, folder))
    {
        //queue(); thread.spawn();
        window.actions[Space] = [this]{ queue(); };
    }
    void event() {
#if COMPARE
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        Reconstruction& r = reconstructions[index];
#else
        uint index = 0;
        Reconstruction& r = MLTR;
#endif
        r.step();
        const float SSE = ::SSE(referenceVolume, r.x, evaluationOrigin, evaluationSize);
        const float MSE = SSE / SSQ;
        const float PSNR = 10*log10( MSE );
        const uint k = r.k;
        log(str(labels[index]+"\t"_+str(k)+"\t"_+str(MSE)+"\t"_+str(-PSNR)));
        {Locker lock(window.renderLock);
            setWindow( &window );
            window.renderBackground(plot.target); plot[labels[index]].insertMulti(r.time/1000000000.f, -PSNR); plot.render();
#if COMPARE
            rSlices[index].render(); rViews[index].render();
#else
            //rSlices.render(); rViews.render();
#endif
            setWindow( 0 );
        }
        //cylinderCheck(r.x);
#if COMPARE
        queue();
#endif
    }
} app;
