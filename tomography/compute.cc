#include "cdf.h"
//include "SIRT.h"
//include "approximate.h"
#include "adjoint.h"
//include "MLEM.h"
#include "plot.h"
#include "window.h"
#include "layout.h"
#include "graphics.h"
#include "view.h"
#include "error.h"

struct Application : Poll {
    // Parameters
    const uint stride = 1/*4*/;
    const int downsampleFactor = 1;
    const bool downsampleProjections = false;
    //const int3 reconstructionSize = int3(512,512,896) / downsampleFactor;
    const int3 reconstructionSize = int3(128,128,256) / downsampleFactor;

    // Data
    VolumeCDF projectionDataCDF;
    const VolumeF& projectionData = projectionDataCDF.volume;
    const uint projectionCount = projectionData.sampleCount.z;

    array<ImageF> images = sliceProjectionVolume(projectionData, stride, downsampleProjections);
    array<Projection> projections = evaluateProjections(reconstructionSize, images[0].size(), projectionCount, stride);

    // Reconstruction
    string labels[1] = {"Adjoint"_};
    unique<Reconstruction> reconstructions[1] {unique<Adjoint>(reconstructionSize, int3(images.first().size(), projections.size), true, true, "small"_)};
    Thread thread;

    // Evaluation
    array<ImageF> referenceImages = sliceProjectionVolume(projectionData, 1, downsampleProjections);
    array<Projection> referenceProjections = evaluateProjections(reconstructionSize, images[0].size(), projectionCount);
    VolumeCDF referenceVolume {Folder("Small"_, Folder("Results"_, home()))};
    //VolumeCDF filteredBackprojection {Folder("FBP"_, Folder("Results"_, home()))};
    //buffer<Projection> fullSizeProjections = evaluateProjections(filteredBackprojection.volume.sampleCount, images[0].size(), projectionCount);
    //const float SSQ = ::SSQ(/*referenceImages*/images);
    const float SSQ = ::SSQ(referenceVolume.volume);

    int upsample = 4; //downsampleProjections?2:1
    ProjectionView projectionView {referenceImages, upsample};
    //VolumeView filteredBackprojectionView {&filteredBackprojection.volume, fullSizeProjections, upsample}; //TODO: preproject
    VolumeView reconstructionView {&reconstructions[0]->x, referenceProjections, upsample};
    HBox top {{&projectionView, /*&filteredBackprojectionView,*/ &reconstructionView}};
    Plot plot;
    //SliceView filteredBackprojectionSliceView {&filteredBackprojection.volume, upsample};
    SliceView reconstructionSliceView {&reconstructions[0]->x, upsample};
    HBox bottom {{&plot, /*&filteredBackprojectionSliceView,*/ &reconstructionSliceView}};
    VBox layout {{&top, &bottom}};
    Window window {&layout, strx(int3(images.first().size(), projections.size))+" "_+strx(reconstructionSize) , int2(3*projectionView.sizeHint().x,projectionView.sizeHint().y+512)}; // FIXME

    Application(string path) : Poll(0,0,thread), projectionDataCDF(path) { if(reconstructions[0]->k==-1) { queue(); thread.spawn(); } /*else view only*/ }
    void event() {
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        Reconstruction& r = reconstructions[index];
        //FIXME: keep every iterations
        if(r.k>=0) {
            if(existsFile(r.name, r.folder)) removeFile(r.name, r.folder);  // Removes previous evaluation
            rename(r.name+"."_+dec(r.k), r.name, r.folder); // Renames during step to invalidate volume if aborted
            r.step(projections, images);
            const float PSNR = 10*log10( ::SSE(referenceVolume.volume, r.x) / SSQ );
            plot[labels[index]].insert(r.totalTime.toFloat(), -PSNR);
            log("\t", r.totalTime.toFloat(), -PSNR);
        } else {
            r.initialize(projections, images);
        }
        if(existsFile(r.name+"."_+dec(r.k), r.folder)) removeFile(r.name+"."_+dec(r.k), r.folder);  // Removes previous evaluation
        rename(r.name, r.name+"."_+dec(r.k), r.folder);
        window.render();
        /*if(r.k<4)*/ queue();
    }
} app ( arguments()[0] );
