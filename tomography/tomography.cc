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
#include "synthetic.h"

struct Application : Poll {
    map<string, Variant> parameters = parseParameters(arguments(),{"size"_,"proj"_,"subset"_,"noise"_,"double"_,"SART"_,"MLTR"_,"PMLTR"_,"stop"_,"rotations"_,"radius"_,"adaptive"_});

    // Reference volume
    int3 volumeSize = fromInt3(parameters.value("size"_,""_)) ?: 64;
    PorousRock rock {volumeSize, parameters.value("radius"_, 16.f)};
    const CLVolume referenceVolume = rock.volume();
    const float centerSSQ = ::SSQ(referenceVolume,  int3(0,0,volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/2));
    const float extremeSSQ = ::SSQ(referenceVolume, int3(0,0,0), int3(volumeSize.xy(), volumeSize.z/4)) + ::SSQ(referenceVolume, int3(0,0, 3*volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/4));

    // Projection
    Folder folder = existsFolder("Cache"_) ? "Cache"_ : Folder();
    const int3 projectionSize = fromInt3(parameters.value("proj"_,""_)) ?: volumeSize;
    buffer<Projection> projections = getProjections();
    buffer<Projection> getProjections() { // Evaluates reconstruction performance at all grid points spanned by given parameter ranges
        //{volSize=64..512}x{projSize=64..512}(4:3)x{single, double, adaptive}×{rotations=1..5}×{noise=8..16}×{subsetSize=8..32}
        array<Projection> projections;
        projections << Projection(volumeSize, projectionSize, parameters.contains("double"_)?Projection::Double:parameters.contains("adaptive"_)?Projection::Adaptive:Projection::Single, parameters.value("rotations"_, 1), 1<<parameters.value("noise"_,0));
        return move(projections);
    }

    buffer<ImageArray> projectionData = apply(projections, [this](const Projection& A){ return load(A); }); // Map projection data files, convert to transmission data, simulate noise and upload to device
    ImageArray load(const Projection& A) {
        VolumeF source;
        if(folder) { // Persistent cache is enabled
            if(!existsFile(str(A), folder)) {
                VolumeF projectionData (A.projectionSize, Map(File(str(A), folder, Flags(ReadWrite|Create)).resize(A.projectionSize.z*A.projectionSize.y*A.projectionSize.x*sizeof(float)), Map::Prot(Map::Read|Map::Write)), "lnb"_);
                Time time;
                for(uint index: range(projectionData.size.z)) { log(index); rock.project(::slice(projectionData, index), A, index, rock.factor); }
                log("A", time);
            }
            assert_(File(str(A), folder).size() == A.projectionSize.z*A.projectionSize.y*A.projectionSize.x*sizeof(float));
            source = VolumeF(projectionSize, Map(str(A), folder));
        } else { // Anonymous memory (not persistent)
            source = VolumeF(A.projectionSize, "lnb"_);
            Time time;
            for(uint index: range(source.size.z)) { log(index); rock.project(::slice(source, index), A, index, rock.factor); }
            log("A", time);
        }
        VolumeF target(projectionSize, 0, "b"_);
        Time time;
        for(uint i: range(target.data.size)) {
            float x = source.data[i];
            assert_(x>=0 && x<expOverflow, x);
            target.data[i] = A.photonCount>1 ? poisson(A.photonCount * exp(-x)) / A.photonCount : exp(-x); //TODO: CL noise (also would remove one of the two copies)
        }
        log("Poisson", time);
        return ImageArray(target);
    }

    // Reconstruction
    const uint subsetSize = parameters.value("subset"_,round(sqrt(float(projectionSize.z))));
    buffer<unique<SubsetReconstruction>> reconstructions = getReconstructions();
    buffer<unique<SubsetReconstruction>> getReconstructions() {
        array<unique<SubsetReconstruction>> reconstructions;
        for(const uint index: range(projections.size)) {
            if(parameters.contains("SART"_)) reconstructions << unique<SART>(projections[index], projectionData[index], subsetSize);
            if(parameters.contains("MLTR"_)) reconstructions << unique<MLTR>(projections[index], projectionData[index], subsetSize);
            if(parameters.contains("PMLTR"_)) reconstructions << unique<PMLTR>(projections[index], projectionData[index], subsetSize);
        }
        assert_(reconstructions);
        return move(reconstructions);
    }
    Thread thread {19};

    // Interface
    buffer<const CLVolume*> volumes = apply(reconstructions, [&](const SubsetReconstruction& r){ return &r.x;});
    int upsample = max(1, 256 / projectionSize.x);

    Value sliceIndex = Value((volumeSize.z-1) / 2);
    SliceView x {&referenceVolume, upsample, sliceIndex};
    HList<SliceView> rSlices { apply<SliceView>(volumes, upsample, sliceIndex, 0/*2*mean*/) };
    HBox slices {{&x, &rSlices}};

    Value projectionIndex = Value((projectionSize.z-1) / 2);
    const ImageArray b = negln(projectionData[0]);
    SliceView bView {&b, upsample, projectionIndex};
    HList<VolumeView> rViews { apply<VolumeView>(volumes, Projection(volumeSize, projectionSize, Projection::Single, 1), upsample, projectionIndex) };
    HBox views {{&bView, &rViews}};

    Plot plot;

    VBox layout {{&slices, &views, &plot}};
    Window window {&layout, str(strx(volumeSize), strx(projectionSize), strx(int2(projectionSize.z/subsetSize, subsetSize))), int2(-1, -1024)};
    bool wait = false;

    Application() : Poll(0,0,thread) {
        log(parameters);
        queue(); thread.spawn();
        window.actions[Space] = [this]{ for(Reconstruction& r: reconstructions) { r.divergent=0; if(r.time==uint64(-1)) r.time=r.stopTime; }/*Force diverging iteration*/ if(wait) { wait=false; queue(); } else wait=true; };
        window.actions[RightArrow] = [this]{ for(Reconstruction& r: reconstructions) { r.divergent=0; if(r.time==uint64(-1)) r.time=r.stopTime; } /*Force diverging iteration*/ queue(); };
    }
    ~Application() {
        for(SubsetReconstruction& r: reconstructions) log(r, centerSSQ, extremeSSQ);
    }

    void event() {
        assert_(reconstructions);
        uint index = argmin(reconstructions);
        SubsetReconstruction& r = reconstructions[index];
        const uint stop = parameters.value("stop"_,64);
        if(r.divergent && r.k >= stop) {
            for(SubsetReconstruction& r: reconstructions) log(r, centerSSQ, extremeSSQ);
            log("Divergent"_);
            return;
        }
        if(r.time==uint64(-1)) r.time=r.stopTime;
        if(r.k >= 64*(1+log2(r.subsetCount))) { log("Asymptotic"); return; } // All reconstructions stopped converging or first to complete several supersteps
        r.step();
        float centerSSE = ::SSE(referenceVolume, r.x, int3(0,0,volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/2));
        float extremeSSE = ::SSE(referenceVolume, r.x, int3(0,0,0), int3(volumeSize.xy(), volumeSize.z/4)) + ::SSE(referenceVolume, r.x, int3(0,0, 3*volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/4));
        if(centerSSE > r.centerSSE && extremeSSE > r.extremeSSE) { r.divergent++; r.stopTime=r.time; if(r.k >= stop) r.time=-1; } else r.divergent=0;
        r.centerSSE = centerSSE; r.bestCenterSSE = min(r.bestCenterSSE, centerSSE);
        r.extremeSSE = extremeSSE; r.bestExtremeSSE = min(r.bestExtremeSSE, extremeSSE);
        if(centerSSE + extremeSSE < r.centerSSE + r.extremeSSE) r.bestK=r.k;
        log(str(r,centerSSQ, extremeSSQ));
        writeFile(str(r), bestNMSE(r, centerSSQ, extremeSSQ), Folder("Results"_));
        {Locker lock(window.renderLock);
            setWindow( &window );
            float NMSE = (r.bestCenterSSE+r.bestExtremeSSE)/(centerSSQ+extremeSSQ);
            window.renderBackground(plot.target); plot[r.x.name].insertMulti(r.time/1000000000.f, NMSE); plot.render();
            rSlices[index].render(); rViews[index].render();
            setWindow(0);
        }
        if(!wait) queue();
    }
} app;
