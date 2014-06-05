#include "cdf.h"
#include "approximate.h"
#include "plot.h"
#include "window.h"
#include "layout.h"
#include "graphics.h"
#include "view.h"
#include "sum.h"

struct Application : Poll {
    // Parameters
    const uint stride = 1/*4*/;
    const int downsampleFactor = 1;
    const bool downsampleProjections = false;
    //const int3 volumeSize = int3(512,512,896) / downsampleFactor;
    const int3 volumeSize = int3(128,128,256) / downsampleFactor;

    // Data
    const VolumeF projectionData;
    const uint projectionCount = projectionData.size.z;

    buffer<Projection> projections;

    // Reconstruction
    string labels[1] = {"Adjoint"_};
    unique<Reconstruction> reconstructions[1] {unique<Approximate>(volumeSize, projections, projectionData, true, true, "small"_)};
    Thread thread;

    // Evaluation
    const VolumeF referenceVolume = loadCDF(Folder("Small"_, Folder("Results"_, home())));
    const float SSQ = ::SSQ(referenceVolume);

    int upsample = 4; //downsampleProjections?2:1
    uint projectionIndex = 0;
    SliceView projectionView {&projectionData, upsample, projectionIndex};
    VolumeView reconstructionView {&reconstructions[0]->x, projections, projectionData.size.xy(), upsample, projectionIndex};
    HBox top {{&projectionView, &reconstructionView}};
    Plot plot;
    uint sliceIndex = 0;
    SliceView referenceSliceView {&referenceVolume, upsample, sliceIndex};
    SliceView reconstructionSliceView {&reconstructions[0]->x, upsample, sliceIndex};
    HBox bottom {{&plot, &referenceSliceView, &reconstructionSliceView}};
    VBox layout {{&top, &bottom}};
    Window window {&layout, strx(projectionData.size)+" "_+strx(volumeSize) , int2(3*projectionView.sizeHint().x,projectionView.sizeHint().y+512)}; // FIXME

    Application(string path) : Poll(0,0,thread), projectionData(loadCDF(path)), projections(evaluateProjections(volumeSize, projectionData.size.xy(), projectionCount, stride, true)) { queue(); thread.spawn(); }
    void event() {
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        Reconstruction& r = reconstructions[index];
        r.step();
        const float PSNR = 10*log10( ::SSE(referenceVolume, r.x) / SSQ );
        plot[labels[index]].insert(r.totalTime.toFloat(), -PSNR);
        log("\t", r.totalTime.toFloat(), -PSNR);
        window.render();
        //queue();
    }
} app ( arguments()[0] );
