#include "phantom.h"
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

/*inline float SSQ(const VolumeF& x) {
    const float* xData = x;
    float SSQ[coreCount] = {};
    chunk_parallel(x.size(), [&](uint id, uint offset, uint size) {
        float accumulator = 0;
        for(uint i: range(offset,offset+size)) accumulator += sq(xData[i]);
        SSQ[id] += accumulator;
    });
    return sum(SSQ);
}

inline float SSE(const VolumeF& a, const VolumeF& b) {
    assert_(a.size() == b.size());
    const float* aData = a; const float* bData = b;
    float SSE[coreCount] = {};
    chunk_parallel(a.size(), [&](uint id, uint offset, uint size) {
        float accumulator = 0;
        for(uint i: range(offset,offset+size)) accumulator += sq(aData[i] - bData[i]);
        SSE[id] += accumulator;
    });
    return sum(SSE);
}*/

inline float SSQ(const ref<ImageF>& images) {
    float SSQ[coreCount] = {};
    parallel(images, [&](uint id, const ImageF& image) {
        float accumulator = 0;
        for(uint i: range(image.data.size)) accumulator += sq(image.data[i]);
        SSQ[id] += accumulator;
    });
    return sum(SSQ);
}

inline float SSE(const ref<ImageF>& A, const ref<ImageF>& B) {
    assert_(A.size == B.size);
    float SSE[coreCount] = {};
    parallel(A.size, [&](uint id, uint index) {
        float accumulator = 0;
        const ImageF& a = A[index];
        const ImageF& b = B[index];
        assert(a.data.size == b.data.size);
        for(uint i: range(a.data.size)) accumulator += sq(a.data[i] - b.data[i]);
        SSE[id] += accumulator;
    });
    return sum(SSE);
}

inline float SSQ(const VolumeF& volume, const ref<Projection>& projections) {
    float SSQ[coreCount] = {};
    parallel(projections.size, [&](uint id, uint projectionIndex) {
        float accumulator = 0;
        ImageF image = ImageF(projections[projectionIndex].imageSize);
        project(image, volume, projections[projectionIndex]);
        for(uint i: range(image.data.size)) accumulator += sq(image.data[i]);
        SSQ[id] += accumulator;
    });
    return sum(SSQ);
}


inline float SSE(const VolumeF& volume, const ref<Projection>& projections, const ref<ImageF>& references) {
    float SSE[coreCount] = {};
    parallel(projections.size, [&](uint id, uint projectionIndex) {
        float accumulator = 0;
        ImageF image = ImageF(projections[projectionIndex].imageSize);
        project(image, volume, projections[projectionIndex]);
        const ImageF& reference = references[projectionIndex];
        assert(image.data.size == reference.data.size);
        for(uint i: range(reference.data.size)) accumulator += sq(image.data[i] - reference.data[i]);
        SSE[id] += accumulator;
    });
    return sum(SSE);
}

struct Application : Poll {
    // Parameters
    const uint stride = 4;
    const int downsampleFactor = 1;
    const bool downsampleProjections = false;
    const int3 reconstructionSize = int3(512,512,896) / downsampleFactor;

    // Data
    VolumeCDF projectionDataCDF {Folder("Preprocessed"_, Folder("Data"_, home()))};
    const VolumeF& projectionData = projectionDataCDF.volume;
    const uint projectionCount = projectionData.sampleCount.z;

    array<ImageF> images = sliceProjectionVolume(projectionData, stride, downsampleProjections);
    array<Projection> projections = evaluateProjections(reconstructionSize, images[0].size(), projectionCount, stride);

    // Reconstruction
    string labels[1] = {"Adjoint"_};
    unique<Reconstruction> reconstructions[1] {unique<Adjoint>(reconstructionSize, int3(images.first().size(), projections.size), true, true)};
    Thread thread;

    // Evaluation
    array<ImageF> referenceImages = sliceProjectionVolume(projectionData, 1, downsampleProjections);
    array<Projection> referenceProjections = evaluateProjections(reconstructionSize, images[0].size(), projectionCount);
    VolumeCDF filteredBackprojection {Folder("FBP"_, Folder("Results"_, home()))};
    buffer<Projection> fullSizeProjections = evaluateProjections(filteredBackprojection.volume.sampleCount, images[0].size(), projectionCount);
    const float SSQ = ::SSQ(/*referenceImages*/images);

    ProjectionView projectionView {referenceImages, downsampleProjections?2:1};
    VolumeView filteredBackprojectionView {filteredBackprojection.volume, fullSizeProjections, downsampleProjections?2:1}; //TODO: preproject
    VolumeView reconstructionView {reconstructions[0]->x, referenceProjections, downsampleProjections?2:1};
    HBox top {{&projectionView, &filteredBackprojectionView, &reconstructionView}};
    Plot plot;
    SliceView filteredBackprojectionSliceView {filteredBackprojection.volume, downsampleProjections?2:1};
    SliceView reconstructionSliceView {reconstructions[0]->x, downsampleFactor};
    HBox bottom {{&plot, &filteredBackprojectionSliceView, &reconstructionSliceView}};
    VBox layout {{&top, &bottom}};
    Window window {&layout, strx(int3(images.first().size(), projections.size))+" "_+strx(reconstructionSize) , int2(3*projectionView.sizeHint().x,projectionView.sizeHint().y+512)}; // FIXME

    Application() : Poll(0,0,thread) { if(reconstructions[0]->k==-1) { queue(); thread.spawn(); } /*else view only*/ }
    void event() {
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        Reconstruction& r = reconstructions[index];
        if(r.k>=0) {
            if(existsFile(r.name, r.folder)) removeFile(r.name, r.folder);  // Removes previous evaluation
            rename(r.name+"."_+dec(r.k), r.name, r.folder); // Renames during step to invalidate volume if aborted
            r.step(projections, images);
        } else {
            r.initialize(projections, images);
        }
        if(existsFile(r.name+"."_+dec(r.k), r.folder)) removeFile(r.name+"."_+dec(r.k), r.folder);  // Removes previous evaluation
        rename(r.name, r.name+"."_+dec(r.k), r.folder);
        //Time time;
        //const float PSNR = 10*log10(SSQ / ::SSE(r.x, projections, images));
        //const float PSNR = 10*log10(SSQ / ::SSE(r.x, referenceProjections, referenceImages));
        //plot[labels[index]].insert(r.totalTime.toFloat(), PSNR);
        //log("\t", PSNR, "\tEvaluation:",time);
        window.render();
        if(r.k<10) queue();
    }
} app;
