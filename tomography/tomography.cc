#include "phantom.h"
#include "cdf.h"
//include "SIRT.h"
//#include "approximate.h"
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
    const uint stride = 32;
    const int downsampleFactor = 4;
    const int3 reconstructionSize = int3(512,512,896) / downsampleFactor;

    VolumeCDF projectionDataCDF {Folder("Preprocessed"_, Folder("Data"_, home()))};
    const VolumeF& projectionData = projectionDataCDF.volume;
    const uint projectionCount = projectionData.sampleCount.z;
    //Phantom phantom {16};

    array<ImageF> images = sliceProjectionVolume(projectionData, stride);
    array<Projection> projections = evaluateProjections(reconstructionSize, images[0].size(), projectionCount, stride);

    string labels[1] = {"Adjoint"_};
    unique<Reconstruction> reconstructions[1] {unique<Adjoint>(reconstructionSize, true, true)};

    Thread thread;

    Application() : Poll(0,0,thread) {
        /*for(int index: range(reconstructionSize.z)) {
            const int2 imageSize = int2(504, 378)/2;
            projections << Projection(reconstructionSize, imageSize, index*total_num_projections/reconstructionSize.z);
            images << phantom.project(imageSize, projections.last());*/
        log(reconstructionSize, projections.size, images.first().size());
        for(auto& reconstruction: reconstructions) reconstruction->initialize(projections, images);
        queue();
        //reconstructions[0]->x = phantom.volume(reconstructionSize);
        thread.spawn();
    }

    VolumeCDF filteredBackprojection {Folder("FBP"_, Folder("Results"_, home()))};
    const float SSQ = ::SSQ(images); //::SSQ(filteredBackprojection.volume);
    ProjectionView projectionView {images, 2};
    buffer<Projection> fullSizeProjections = evaluateProjections(filteredBackprojection.volume.sampleCount, images[0].size(), projectionCount, stride); //FIXME
    VolumeView filteredBackprojectionView {filteredBackprojection.volume, fullSizeProjections, 2}; //TODO: preproject
    VolumeView reconstructionView {reconstructions[0]->x, projections, 2};
    Plot plot;
    SliceView filteredBackprojectionSliceView {filteredBackprojection.volume, 2};
    SliceView reconstructionSliceView {reconstructions[0]->x, 2};
    WidgetGrid layout {{&projectionView, &filteredBackprojectionView, &reconstructionView,&plot,&filteredBackprojectionSliceView,&reconstructionSliceView}};
    Window window {&layout, "Compute"_, int2(3,2)*/*projectionView.sizeHint()*/int2(512,512)}; // FIXME


    void event() {
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        reconstructions[index]->step(projections, images);
        const float PSNR = 10*log10(SSQ / ::SSE(reconstructions[index]->x, projections, images));
        plot[labels[index]].insert(reconstructions[index]->totalTime.toFloat(), PSNR);
        log("\t", PSNR);
        window.render();
        queue();
    }
} app;
