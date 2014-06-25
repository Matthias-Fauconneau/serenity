#include "SART.h"
#include "MLTR.h"
#include "PMLTR.h"

#include "operators.h"
#include "random.h"

#include "plot.h"
#include "layout.h"
#include "window.h"
#include "view.h"

struct Application : Poll {
    map<string, Variant> parameters = parseParameters(arguments());
    Thread thread {19};
    // Reference volume
    Folder folder {"Data"_};
    const uint N = arguments() ? fromInteger(arguments()[0]) : 64;
    int3 volumeSize = N;
    const CLVolume referenceVolume {volumeSize, Map(strx(volumeSize)+".ref"_, folder)};
    int3 evaluationOrigin =  int3(0,0,volumeSize.z/4), evaluationSize = int3(volumeSize.xy(), volumeSize.z/2);
    //int3 evaluationOrigin =  int3(0,0,0), evaluationSize = volumeSize;
    const float mean = ::mean(referenceVolume);
    const float SSQ = ::SSQ(referenceVolume, evaluationOrigin, evaluationSize);
    // Projection
    const int3 projectionSize = N;
    Projection projections[1] = {Projection(volumeSize, projectionSize, parameters.value("double"_, false), parameters.value("rotations"_, 1), parameters.value("photonCount"_,0))};
    buffer<ImageArray> projectionData = apply(ref<Projection>(projections), [this](const Projection& A){ return load(A); }); // Map projection data files, convert to transmission data, simulate noise and upload to device
    ImageArray load(const Projection& A) {
        VolumeF source(projectionSize, Map(str(A), folder));
        VolumeF target(projectionSize, 0, "b"_);
        for(uint i: range(target.data.size)) {
            float x = source.data[i];
            assert_(x>=0 && x<expOverflow, x);
            target.data[i] = A.photonCount ? poisson(A.photonCount * exp(-x)) / A.photonCount : exp(-x); //TODO: CL noise (also would remove one of the two copies)
        }
        return ImageArray(target);
    }

    //const uint subsetSize = 1;
    //const uint subsetSize = projectionSize.z;
    const uint subsetSize = parameters.value("subset"_,round(sqrt(float(projectionSize.z))));
    unique<SubsetReconstruction> reconstructions[1] {unique<SART>(projections[0], projectionData[0], subsetSize)/*, unique<MLTR>(projections[0], projectionData[0], subsetSize), unique<PMLTR>(projections[0], projectionData[0], subsetSize)*/};

    // Interface
    buffer<const CLVolume*> volumes = apply(ref<unique<SubsetReconstruction>>(reconstructions), [&](const SubsetReconstruction& r){ return &r.x;});
    int upsample = max(1, 512 / projectionSize.x);

    Value sliceIndex = Value((volumeSize.z-1) / 2);
    SliceView x {&referenceVolume, upsample, sliceIndex};
    HList<SliceView> rSlices { apply<SliceView>(volumes, upsample, sliceIndex, 0/*2*mean*/) };
    HBox slices {{&x, &rSlices}};

    Value projectionIndex = Value((projectionSize.z-1) / 2);
    VolumeView b {&referenceVolume, Projection(volumeSize, projectionSize), upsample, projectionIndex};
    HList<VolumeView> rViews { apply<VolumeView>(volumes, Projection(volumeSize, projectionSize), upsample, projectionIndex) };
    HBox views {{&b, &rViews}};

    Plot plot;

    VBox layout {{&slices, &views, &plot}};
    Window window {&layout, str(N), int2(-1, -1024)};
    bool wait = false;

    Application() : Poll(0,0,thread) {
        log(parameters);
        queue(); thread.spawn();
        window.actions[Space] = [this]{ for(Reconstruction& r: reconstructions) { r.divergent=0; if(r.time==uint64(-1)) r.time=r.stopTime; }/*Force diverging iteration*/ if(wait) { wait=false; queue(); } else wait=true; };
        window.actions[RightArrow] = [this]{ for(Reconstruction& r: reconstructions) { r.divergent=0; if(r.time==uint64(-1)) r.time=r.stopTime; } /*Force diverging iteration*/ queue(); };
    }
    void event() {
        uint index = argmin(mref<unique<SubsetReconstruction>>(reconstructions));
        SubsetReconstruction& r = reconstructions[index];
        if(r.divergent) { log("Divergent"_); return; }
        if(r.k >= 256*r.subsetCount) { log("Asymptotic"); return; } // All reconstructions stopped converging or first to complete several supersteps
        r.step();
        const float SSE = ::SSE(referenceVolume, r.x, evaluationOrigin, evaluationSize);
        const float MSE = SSE / SSQ;
        const float PSNR = 10*log10(MSE);
        String info;
        if(ref<unique<SubsetReconstruction>>(reconstructions).size>1) info << r.x.name << '\t';
        if(r.subsetCount>1) info << str(r.subsetSize) << '\t';
        log(info+str(r.k)+"\t"_+str(-PSNR));
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
