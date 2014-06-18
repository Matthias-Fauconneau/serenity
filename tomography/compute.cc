#include "algebraic.h"
#include "conjugate.h"
#include "MLTR.h"

#include "plot.h"
#include "window.h"
#include "layout.h"
#include "graphics.h"
#include "view.h"
#include "operators.h"

const Folder& folder = "Data"_;
const uint N = arguments() ? fromInteger(arguments()[0]) : 64;

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

#if COMPARE
    HList<SliceView> rSlices {apply(ref<unique<Reconstruction>>(reconstructions), [&](const Reconstruction& r){ return SliceView(r.x, upsample, sliceIndex);})};
#else
    SliceView slices[10] {
        {referenceVolume, upsample, sliceIndex, "x0"_},
        {MLTR.Ax, upsample, subsetIndex,"Ax"_}, {MLTR.b, upsample, subsetIndex,"b"_}, {MLTR.r, upsample, subsetIndex,"r=exp(-Ax) - exp(-b)"_}, {MLTR.Atr, upsample, sliceIndex,"Atr"_},
        {MLTR.w, upsample, subsetIndex,"Ai"_}, {MLTR.w, upsample, subsetIndex,"w=Ai exp(-Ax)"_}, {MLTR.Atw, upsample, sliceIndex,"Atw"_}, {MLTR.AtrAtw, upsample, sliceIndex,"Atr / Atw"_}, {MLTR.x, upsample, sliceIndex,"x"_}};
#endif

#if COMPARE
    HList<VolumeView> rViews {apply(ref<unique<Reconstruction>>(reconstructions), [&](const Reconstruction& r){ return VolumeView(r.x, projectionData.size, upsample, projectionIndex);})};
#else
    SliceView b {projectionData, upsample, projectionIndex, "b"_};
    VolumeView views[3] {{MLTR.Atr, projectionData.size, upsample, projectionIndex, "Atr"_}, {MLTR.Atw, projectionData.size, upsample, projectionIndex, "Atw"_}, {MLTR.x, projectionData.size, upsample, projectionIndex, "x"_}};
#endif

    WidgetGrid grid {apply(mref<SliceView>(slices),[](SliceView& o)->Widget*{return &o;})+ref<Widget*>{&b}+apply(mref<VolumeView>(views),[](VolumeView& o)->Widget*{return &o;})};
    Plot plot;

    VBox layout {{&grid, &plot}};

    Window window {&layout, strx(projectionData.size)+" "_+strx(size)}; // FIXME

    Application() : Poll(0,0,thread),
        projectionData(int3(N,N,N), Map(strx(int3(N,N,N))+".proj"_, folder)),
        referenceVolume(int3(N,N,N), Map(strx(int3(N,N,N))+".ref"_, folder))
    {
        queue(); thread.spawn();
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
            grid.render();
#endif
            setWindow( 0 );
        }
        //cylinderCheck(r.x);
#if COMPARE
        queue();
#endif
    }
} app;
