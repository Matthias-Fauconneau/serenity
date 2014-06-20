#include "SART.h"
#include "MART.h"
#include "MLEM.h"
#include "CG.h"
#include "MLTR.h"
#include "PMLTR.h"

#include "operators.h"
#include "random.h"

#include "plot.h"
#include "layout.h"
#include "window.h"
#include "view.h"
#include "png.h"

/// Projects with Poisson noise
ImageArray project(const Projection& A, const CLVolume& x, const int oversample) {
    VolumeF Ax(A.projectionSize);
    for(uint index: range(Ax.size.z)) {
        log(index);
        ImageF slice = ::slice(Ax, index);
        if(oversample==2) {
            ImageF fullSize(oversample*A.projectionSize.xy());
            ::project(fullSize, A, x, index);
            downsample(slice, fullSize); //TODO: CL downsample
        } else if(oversample==1) {
            ::project(slice, A, x, index);
        } else error(oversample);
        for(float& y: slice.data) y = A.photonCount ? poisson(A.photonCount * exp(-y)) / A.photonCount : exp(-y); //TODO: CL noise
    }
    return Ax;
}

struct Application : Poll {
    Thread thread {19};
    // Reference volume
    Folder folder {"Data"_};
    const uint N = arguments() ? fromInteger(arguments()[0]) : 64;
    int3 volumeSize = N;
    const CLVolume referenceVolume {volumeSize, Map(strx(volumeSize)+".ref"_, folder)};
    const int oversample = 1; // 2: oversample
    const CLVolume acquisitionVolume {oversample*volumeSize, Map(strx(oversample*volumeSize)+".ref"_, folder)};
    //int3 evaluationOrigin =  int3(0,0,volumeSize.z/4), evaluationSize = int3(volumeSize.xy(), volumeSize.z/2);
    int3 evaluationOrigin =  int3(0,0,0), evaluationSize = volumeSize;
    const float SSQ = ::SSQ(referenceVolume, evaluationOrigin, evaluationSize);
    // Projection
    const int3 projectionSize = N;
    Projection projections[1] = {Projection(volumeSize, projectionSize, /*doubleHelix*/true, /*pitch*/2)};
    buffer<ImageArray> projectionData = apply(ref<Projection>(projections), [this](Projection p) {p.volumeSize = acquisitionVolume.size; return project(p, acquisitionVolume, oversample);});

    const uint subsetSize = round(sqrt(float(projectionSize.z)));
    unique<Reconstruction> reconstructions[3] {unique<SART>(projections[0], projectionData[0], subsetSize), unique<MLTR>(projections[0], projectionData[0], subsetSize), unique<PMLTR>(projections[0], projectionData[0], subsetSize)};

    // Interface
    buffer<const CLVolume*> volumes = apply(ref<unique<Reconstruction>>(reconstructions), [&](const Reconstruction& r){ return &r.x;});
    int upsample = max(1, 256 / projectionSize.x);

    Value sliceIndex = (volumeSize.z-1) / 2;
    SliceView x {&referenceVolume, upsample, sliceIndex};
    HList<SliceView> rSlices { apply<SliceView>(volumes, upsample, sliceIndex) };
    HBox slices {{&x, &rSlices}};

    Value projectionIndex = (projectionSize.z-1) / 2;
    VolumeView b {&referenceVolume, Projection(volumeSize, projectionSize), upsample, projectionIndex};
    HList<VolumeView> rViews { apply<VolumeView>(volumes, Projection(volumeSize, projectionSize), upsample, projectionIndex) };
    HBox views {{&b, &rViews}};

    Plot plot;

    VBox layout {{&slices, &views, &plot}};
    Window window {&layout, strx(volumeSize), int2(-1, -1024)};
    bool wait = false;

    Application() : Poll(0,0,thread) {
        queue(); thread.spawn();
        window.actions[Space] = [this]{ for(Reconstruction& r: reconstructions) { r.divergent=0; if(r.time==uint64(-1)) r.time=r.stopTime; }/*Force diverging iteration*/ if(wait) { wait=false; queue(); } else wait=true; };
        window.actions[RightArrow] = [this]{ for(Reconstruction& r: reconstructions) { r.divergent=0; if(r.time==uint64(-1)) r.time=r.stopTime; } /*Force diverging iteration*/ queue(); };
        window.actions[PrintScreen] = [this]{ Locker lock(window.renderLock); writeFile("snapshot.png"_,encodePNG(window.target)); };
    }
    void event() {
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        Reconstruction& r = reconstructions[index];
        if(r.divergent) return; // All reconstructions stopped converging
        r.step();
        const float SSE = ::SSE(referenceVolume, r.x, evaluationOrigin, evaluationSize);
        const float MSE = SSE / SSQ;
        const float PSNR = 10*log10(MSE);
        log(r.x.name+"\t"_+str(r.k)+"\t"_+str(MSE)+"\t"_+str(-PSNR));
        {Locker lock(window.renderLock);
            setWindow( &window );
            window.renderBackground(plot.target); plot[r.x.name].insertMulti(r.time/1000000000.f, -PSNR); plot.render();
            rSlices[index].render(); rViews[index].render();
            setWindow(0);
        }
        if(SSE > r.SSE) { r.divergent++; r.stopTime=r.time; r.time=-1; log(r.x.name, SSE, r.SSE, SSE>r.SSE, r.divergent); } else r.divergent=0;
        r.SSE = SSE;
        if(!wait) queue();
    }
} app;
