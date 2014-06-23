#include "SART.h"
#include "MLTR.h"
#include "PMLTR.h"

#include "operators.h"
#include "random.h"

#include "plot.h"
#include "layout.h"
#include "window.h"
#include "view.h"

/// Projects with Poisson noise
ImageArray project(Projection A, const CLVolume& x, const int oversample) {
    VolumeF Ax(A.projectionSize);
    A.volumeSize *= oversample;
    A.projectionSize *= oversample;
    for(uint index: range(Ax.size.z)) {
        ImageF slice = ::slice(Ax, index);
        if(oversample==2) {
            ImageF fullSize(A.projectionSize.xy());
            ::project(fullSize, A, x, index);
            downsample(slice, fullSize); //TODO: CL downsample
        } else if(oversample==1) {
            ::project(slice, A, x, index);
        } else error(oversample);
        for(float& y: slice.data) y = A.photonCount ? poisson(A.photonCount * exp(-y)) / A.photonCount : exp(-y); //TODO: CL noise
    }
    return Ax;
}

struct Variant : String {
    Variant(){}
    default_move(Variant);
    Variant(String&& s) : String(move(s)) {}
    Variant(double decimal) : String(ftoa(decimal)){}
    explicit operator bool() const { return size; }
    operator int() const { return *this ? fromInteger(*this) : 0; }
    operator uint() const { return *this ? fromInteger(*this) : 0; }
    operator float() const { return fromDecimal(*this); }
    operator double() const { return fromDecimal(*this); }
    generic operator T() const { return T((const string&)*this); } // Enables implicit conversion to any type with an implicit string constructor
};

// Parses process arguments into parameter=value pairs
map<string, Variant> parseParameters(const ref<string> arguments) {
    map<string, Variant> parameters;
    for(const string& argument: arguments) {
        TextData s (argument);
        string key = s.until("="_);
        if(s) { // Explicit argument
            string value = s.untilEnd();
            parameters.insert(key, Variant(String(value)));
        }
    }
    return parameters;
}

struct Application : Poll {
    map<string, Variant> parameters = parseParameters(arguments());
    Thread thread {19};
    // Reference volume
    Folder folder {"Data"_};
    const uint N = arguments() ? fromInteger(arguments()[0]) : 64;
    int3 volumeSize = N;
    const CLVolume referenceVolume {volumeSize, Map(strx(volumeSize)+".ref"_, folder)};
    const int oversample = 2; // 2: oversample
    const CLVolume acquisitionVolume {oversample*volumeSize, Map(strx(oversample*volumeSize)+".ref"_, folder)};
    int3 evaluationOrigin =  int3(0,0,volumeSize.z/4), evaluationSize = int3(volumeSize.xy(), volumeSize.z/2);
    //int3 evaluationOrigin =  int3(0,0,0), evaluationSize = volumeSize;
    const float mean = ::mean(referenceVolume);
    const float SSQ = ::SSQ(referenceVolume, evaluationOrigin, evaluationSize);
    // Projection
    const int3 projectionSize = N;
    Projection projections[1] = {Projection(volumeSize, projectionSize, parameters.value("doubleHelix"_, true), parameters.value("pitch"_, 2), parameters.value("photonCount"_,256))};
    buffer<ImageArray> projectionData = apply(ref<Projection>(projections), [this](const Projection& A) { return project(A, acquisitionVolume, oversample);});

    const uint subsetSize = round(sqrt(float(projectionSize.z)));
    unique<Reconstruction> reconstructions[3] {unique<SART>(projections[0], projectionData[0], subsetSize), unique<MLTR>(projections[0], projectionData[0], subsetSize), unique<PMLTR>(projections[0], projectionData[0], subsetSize)};

    // Interface
    buffer<const CLVolume*> volumes = apply(ref<unique<Reconstruction>>(reconstructions), [&](const Reconstruction& r){ return &r.x;});
    int upsample = max(1, 256 / projectionSize.x);

    Value sliceIndex = (volumeSize.z-1) / 2;
    SliceView x {&referenceVolume, upsample, sliceIndex};
    HList<SliceView> rSlices { apply<SliceView>(volumes, upsample, sliceIndex, 0/*2*mean*/) };
    HBox slices {{&x, &rSlices}};

    Value projectionIndex = (projectionSize.z-1) / 2;
    VolumeView b {&referenceVolume, Projection(volumeSize, projectionSize), upsample, projectionIndex};
    HList<VolumeView> rViews { apply<VolumeView>(volumes, Projection(volumeSize, projectionSize), upsample, projectionIndex) };
    HBox views {{&b, &rViews}};

    Plot plot;

    VBox layout {{&slices, &views, &plot}};
    Window window {&layout, str(N), int2(-1, -1024)};
    bool wait = false;

    Application() : Poll(0,0,thread) {
        queue(); thread.spawn();
        window.actions[Space] = [this]{ for(Reconstruction& r: reconstructions) { r.divergent=0; if(r.time==uint64(-1)) r.time=r.stopTime; }/*Force diverging iteration*/ if(wait) { wait=false; queue(); } else wait=true; };
        window.actions[RightArrow] = [this]{ for(Reconstruction& r: reconstructions) { r.divergent=0; if(r.time==uint64(-1)) r.time=r.stopTime; } /*Force diverging iteration*/ queue(); };
    }
    void event() {
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        Reconstruction& r = reconstructions[index];
        if(r.divergent || r.k >= 256) return; // All reconstructions stopped converging or first completed 256 steps
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
