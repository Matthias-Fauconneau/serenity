#include "SART.h"
#include "MLTR.h"
#include "PMLTR.h"

#include "operators.h"
#include "random.h"

#include "plot.h"
#include "layout.h"
#include "window.h"
#include "view.h"
#include "variant.h"

struct Application : Poll {
    map<string, Variant> parameters = parseParameters(arguments(),{"size"_,"proj"_,"subset"_,"noise"_,"double"_,"SART"_,"MLTR"_});
    Thread thread {19};
    // Reference volume
    Folder folder {"Data"_};
    int3 volumeSize = fromInt3(parameters.value("size"_)) ?: 64;
    const CLVolume referenceVolume {volumeSize, Map(strx(volumeSize)+".ref"_, folder)};
    //int3 evaluationOrigin =  int3(0,0,volumeSize.z/4), evaluationSize = int3(volumeSize.xy(), volumeSize.z/2);
    int3 evaluationOrigin =  int3(0,0,0), evaluationSize = volumeSize;
    const float mean = ::mean(referenceVolume);
    const float SSQ = ::SSQ(referenceVolume, evaluationOrigin, evaluationSize);
    // Projection
    const int3 projectionSize = fromInt3(parameters.value("proj"_)) ?: volumeSize;
    Projection projections[1] = {Projection(volumeSize, projectionSize, parameters.value("double"_, false), parameters.value("rotations"_, 1), 1<<parameters.value("noise"_,0))};
    buffer<ImageArray> projectionData = apply(ref<Projection>(projections), [this](const Projection& A){ return load(A); }); // Map projection data files, convert to transmission data, simulate noise and upload to device
    ImageArray load(const Projection& A) {
        VolumeF source(projectionSize, Map(str(A), folder));
        VolumeF target(projectionSize, 0, "b"_);
        for(uint i: range(target.data.size)) {
            float x = source.data[i];
            assert_(x>=0 && x<expOverflow, x);
            target.data[i] = A.photonCount>1 ? poisson(A.photonCount * exp(-x)) / A.photonCount : exp(-x); //TODO: CL noise (also would remove one of the two copies)
        }
        return ImageArray(target);
    }

    //const uint subsetSize = 1;
    //const uint subsetSize = projectionSize.z;
    const uint subsetSize = parameters.value("subset"_,round(sqrt(float(projectionSize.z))));
    buffer<unique<SubsetReconstruction>> reconstructions = getReconstructions();
    buffer<unique<SubsetReconstruction>> getReconstructions() {
        array<unique<SubsetReconstruction>> reconstructions;
        if(parameters.contains("SART"_)) reconstructions << unique<SART>(projections[0], projectionData[0], subsetSize);
        if(parameters.contains("MLTR"_)) reconstructions << unique<MLTR>(projections[0], projectionData[0], subsetSize);
        assert_(reconstructions);
        return move(reconstructions);
    }

    // Interface
    buffer<const CLVolume*> volumes = apply(reconstructions, [&](const SubsetReconstruction& r){ return &r.x;});
    int upsample = max(1, 256 / projectionSize.x);

    Value sliceIndex = Value((volumeSize.z-1) / 2);
    SliceView x {&referenceVolume, upsample, sliceIndex};
    HList<SliceView> rSlices { apply<SliceView>(volumes, upsample, sliceIndex, 0/*2*mean*/) };
    SliceView AAti {&(const CLVolume&)((const SART*)(reconstructions[0].pointer))->AAti[1], upsample};
    Value subIndex = Value(0);
    SliceView b2 {&(const CLVolume&)((const SART*)(reconstructions[0].pointer))->subsets[1].b, upsample, subIndex};
    SliceView r {&(const CLVolume&)((const SART*)(reconstructions[0].pointer))->Ax, upsample, subIndex}; // r
    SliceView Atr {&(const CLVolume&)((const SART*)(reconstructions[0].pointer))->Atr, upsample};
    HBox slices {{&x, &rSlices, &AAti, &b2, &r, &Atr}};

    Value projectionIndex = Value((projectionSize.z-1) / 2);
    //VolumeView b {&referenceVolume, Projection(volumeSize, projectionSize, false, 1), upsample, projectionIndex};
    const ImageArray refB = negln(projectionData[0]);
    Value bIndex = Value(0);
    SliceView b {&refB, upsample, bIndex};
    HList<VolumeView> rViews { apply<VolumeView>(volumes, Projection(volumeSize, projectionSize, false, 1), upsample, projectionIndex) };
    HBox views {{&b, &rViews}};

    Plot plot;

    VBox layout {{&slices, &views, &plot}};
    Window window {&layout, str(strx(volumeSize), strx(projectionSize), strx(int2(projectionSize.z/subsetSize, subsetSize))), int2(-1, -1024)};
    bool wait = true;

    Application() : Poll(0,0,thread) {
        log(parameters);
        queue(); thread.spawn();
        window.actions[Space] = [this]{ for(Reconstruction& r: reconstructions) { r.divergent=0; if(r.time==uint64(-1)) r.time=r.stopTime; }/*Force diverging iteration*/ if(wait) { wait=false; queue(); } else wait=true; };
        window.actions[RightArrow] = [this]{ for(Reconstruction& r: reconstructions) { r.divergent=0; if(r.time==uint64(-1)) r.time=r.stopTime; } /*Force diverging iteration*/ queue(); };
    }
    ~Application() {
        if(reconstructions.size>1) for(SubsetReconstruction& r: reconstructions) {
            String info;
            info << r.x.name << '\t';
            info << str(r.subsetSize) << '\t';
             log(info+str(r.k)+"\t"_+str(-10*log10(r.SSE / SSQ))+"\t"_+str(-10*log10(r.bestSSE / SSQ))+"\t"_+str((r.bestSSE / SSQ)/pow(2,parameters.value("noise"_,0)))/*Reconstruction variance on input variance*/);
        }
    }

    void event() {
        assert_(reconstructions);
        uint index = argmin(reconstructions);
        SubsetReconstruction& r = reconstructions[index];
        uint stop = 64*(1+log2(r.subsetCount));
        if(r.divergent && r.k>=stop) {
            if(reconstructions.size>1) for(SubsetReconstruction& r: reconstructions) {
                String info;
                info << r.x.name << '\t';
                info << str(r.subsetSize) << '\t';
                 log(info+str(r.k)+"\t"_+str(-10*log10(r.SSE / SSQ))+"\t"_+str(-10*log10(r.bestSSE / SSQ)));
            }
            log("Divergent"_);
            return;
        }
        if(r.time==uint64(-1)) r.time=r.stopTime;
        if(r.k >= stop) { log("Asymptotic"); return; } // All reconstructions stopped converging or first to complete several supersteps
        r.step();
        const float SSE = ::SSE(referenceVolume, r.x, evaluationOrigin, evaluationSize);
        if(SSE < r.bestSSE) r.bestSSE = SSE;
        const float MSE = SSE / SSQ;
        const float PSNR = 10*log10(MSE);
        String info;
        if(reconstructions.size>1) info << r.x.name << '\t';
        if(r.subsetCount>1) info << str(r.subsetSize) << '\t';
        log(info+str(r.k)+"\t"_+str(-PSNR)+"\t"_+str(-10*log10(r.bestSSE / SSQ)));
        {Locker lock(window.renderLock);
            setWindow( &window );
            window.renderBackground(plot.target); plot[r.x.name].insertMulti(r.time/1000000000.f, -PSNR); plot.render();
            rSlices[index].render(); rViews[index].render();
            setWindow(0);
        }
        if(SSE > r.SSE) { if(r.k>=stop) { r.divergent++; r.stopTime=r.time; r.time=-1; } log(r.x.name, SSE, r.SSE, SSE>r.SSE, r.divergent); } else r.divergent=0;
        r.SSE = SSE;
        if(!wait) queue();
    }
} app;
