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
ImageArray project(const Projection& A, const CLVolume& x) {
    ImageArray Ax(A.projectionSize);
    for(uint index: range(Ax.size.z)) {
        log(index);
        ImageF slice = ::slice(Ax, index);
        ImageF fullSize(2*A.projectionSize.xy());
        ::project(fullSize, A, x, index);
        downsample(slice, fullSize);
        for(float& y: slice.data) y = A.photonCount ? poisson(A.photonCount * exp(-y)) / A.photonCount : exp(-y);
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
    const CLVolume oversampledVolume {2*volumeSize, Map(strx(2*volumeSize)+".ref"_, folder)};
    //int3 evaluationOrigin =  int3(0,0,volumeSize.z/4), evaluationSize = int3(volumeSize.xy(), volumeSize.z/2);
    int3 evaluationOrigin =  int3(0,0,0), evaluationSize = volumeSize;
    const float SSQ = ::SSQ(referenceVolume, evaluationOrigin, evaluationSize);
    // Projection
    const int3 projectionSize = int3(N);
    Projection projections[1] = {Projection(volumeSize, projectionSize, /*doubleHelix*/true, /*pitch*/2)};
    buffer<ImageArray> projectionData = apply(ref<Projection>(projections), [this](Projection p) { p.volumeSize = oversampledVolume.size; return project(p, oversampledVolume); });

    const uint subsetSize = round(sqrt(float(projectionSize.z)));
    unique<Reconstruction> reconstructions[4] {unique<SART>(projections[0], projectionData[0], subsetSize), unique<CG>(projections[0], projectionData[0]), unique<MLTR>(projections[0], projectionData[0], subsetSize), unique<PMLTR>(projections[0], projectionData[0], subsetSize)};

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
        window.actions[Space] = [this]{ if(wait) { wait=false; queue(); } else wait=true; };
        window.actions[RightArrow] = [this]{ if(wait) queue(); };
        window.actions[PrintScreen] = [this]{ Locker lock(window.renderLock); writeFile("snapshot.png"_,encodePNG(window.target)); };
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
