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
    Value projectionIndex = 0; //(projectionData.size.z-1) / 2;

    SliceView x {referenceVolume, upsample, sliceIndex};
#if COMPARE
    HList<SliceView> rSlices {apply(ref<unique<Reconstruction>>(reconstructions), [&](const Reconstruction& r){ return SliceView(r.x, upsample, sliceIndex);})};
#else
    HList<SliceView> rSlices {{SliceView(MLTR.Ax, upsample, projectionIndex,"Ax"_),
                    SliceView(MLTR.r, upsample, projectionIndex,"r"_), SliceView(MLTR.Atr, upsample, projectionIndex,"Atr"_),
                    SliceView(MLTR.w, upsample, projectionIndex,"w"_), SliceView(MLTR.Atw, upsample, projectionIndex,"Atw"_),
                    SliceView(MLTR.x, upsample, sliceIndex,"x"_)}};
#endif
    HBox slices {{&x, &rSlices}};

    SliceView b {projectionData, upsample, projectionIndex};
#if COMPARE
    HList<VolumeView> rViews {apply(ref<unique<Reconstruction>>(reconstructions), [&](const Reconstruction& r){ return VolumeView(r.x, projectionData.size, upsample, projectionIndex);})};
#else
    HList<VolumeView> rViews {{VolumeView(MLTR.Atr, projectionData.size, upsample, sliceIndex, "Atr"_), VolumeView(MLTR.Atw, projectionData.size, upsample, sliceIndex, "Atw"_), VolumeView(MLTR.x, projectionData.size, upsample, sliceIndex, "x"_) }};
#endif
    HBox views {{&b, &rViews}};

    Plot plot;

    VBox layout {{&slices, &views, &plot}};

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
            rSlices.render(); rViews.render();
#endif
            setWindow( 0 );
        }
        //cylinderCheck(r.x);
#if COMPARE
        queue();
#endif
    }
} app;
