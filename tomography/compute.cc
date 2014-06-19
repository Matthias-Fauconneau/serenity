#include "SART.h"
#include "MART.h"
#include "MLEM.h"
#include "CG.h"
#include "MLTR.h"
#include "PMLTR.h"

#include "operators.h"
#include "plot.h"
#include "layout.h"
#include "window.h"
#include "view.h"
#include "png.h"

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
    const uint subsetSize = round(sqrt(float(projectionData.size.z)));
    unique<Reconstruction> reconstructions[4] {unique<SART>(size, projectionData,subsetSize), unique<CG>(size, projectionData), unique<MLTR>(size, projectionData,subsetSize), unique<PMLTR>(size, projectionData,subsetSize)};
    //unique<Reconstruction> reconstructions[2] {unique<MART>(size, projectionData,subsetSize), unique<MLEM>(size, projectionData,subsetSize)};

    // Interface
    int upsample = 256 / projectionData.size.x;

    Value sliceIndex = (referenceVolume.size.z-1) / 2;
    Value projectionIndex = (projectionData.size.z-1) / 2;

    SliceView x {&referenceVolume, upsample, sliceIndex};
    buffer<const CLVolume*> volumes = apply(ref<unique<Reconstruction>>(reconstructions), [&](const Reconstruction& r){ return &r.x;});
            //+ ref<const CLVolume*>{{&r.Atr,&r.Atw,&r.Ax,&r.r}};
    HList<SliceView> rSlices { apply<SliceView>(volumes, upsample, sliceIndex) };
    HBox slices {{&x, &rSlices}};
    SliceView b {&projectionData, upsample, projectionIndex};
    HList<VolumeView> rViews { apply<VolumeView>(volumes, projectionData.size, upsample, projectionIndex) };
    HBox views {{&b, &rViews}};
    Plot plot;

    VBox layout {{&slices, &views, &plot}};
    Window window {&layout, strx(projectionData.size)+" "_+strx(size), int2(-1, -1024)};
    bool wait = false;

    Application() : Poll(0,0,thread), projectionData(int3(N,N,N), Map(strx(int3(N,N,N))+".proj"_, folder)), referenceVolume(int3(N,N,N), Map(strx(int3(N,N,N))+".ref"_, folder)) {
        queue(); thread.spawn();
        window.actions[Space] = [this]{ if(wait) { wait=false; queue(); } else wait=true; };
        window.actions[PrintScreen] = [this]{ Locker lock(window.renderLock); writeFile("snapshot.png"_,window.target); };
    }
    void event() {
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        Reconstruction& r = reconstructions[index];
        if(r.time==uint64(-1)) return; // All reconstructions stopped converging
        r.step();
        const float SSE = ::SSE(referenceVolume, r.x, evaluationOrigin, evaluationSize);
        if(SSE > r.SSE) { r.time = -1; queue(); return; } r.SSE = SSE;
        const float MSE = SSE / SSQ;
        const float PSNR = 10*log10( MSE );
        log(str(r.x.name+"\t"_+str(r.k)+"\t"_+str(MSE)+"\t"_+str(-PSNR)));
        {Locker lock(window.renderLock);
            setWindow( &window );
            window.renderBackground(plot.target); plot[r.x.name].insertMulti(r.time/1000000000.f, -PSNR); plot.render();
            rSlices[index].render(); rViews[index].render();
            setWindow(0);
        }
        if(!wait) queue();
    }
} app;
