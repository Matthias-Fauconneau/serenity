#include "cdf.h"
//include "SIRT.h"
#include "approximate.h"
//include "adjoint.h"
//include "MLEM.h"
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
    //const int3 reconstructionSize = int3(512,512,896) / downsampleFactor;
    const int3 reconstructionSize = int3(128,128,256) / downsampleFactor;

    // Data
    const VolumeF projectionData;
    const uint projectionCount = projectionData.size.z;

    inline buffer<Projection> evaluateProjections(int3 volumeSize, int2 imageSize, uint projectionCount, uint stride, bool synthetic) {
        buffer<Projection> projections (projectionCount / stride);
        for(int index: range(projections.size)) {
            float volumeAspectRatio = (float) volumeSize.z / reconstructionSize.x;
            float projectionAspectRatio = (float) imageSize.y / imageSize.x;
            // FIXME: parse from measurement file
            const uint image_width = 2048, image_height = 1536;
            if(!synthetic) assert_(image_height*imageSize.x == image_width*imageSize.y, imageSize, image_height, image_width);
            const float pixel_size = 0.194; // [mm]
            const float detectorHalfWidth = image_width * pixel_size; // [mm] ~ 397 mm
            const float camera_length = 328.811; // [mm]
            const float hFOV = atan(detectorHalfWidth, camera_length); // Horizontal field of view (i.e cone beam angle) [rad] ~ 50°
            const float specimenDistance = 2.78845; // [mm]
            const float volumeRadius = specimenDistance * cos(hFOV); // [mm] ~ 2 mm
            const float voxelRadius = float(volumeSize.x-1)/2;
            const uint num_projections_per_revolution = synthetic ? projectionCount/2 : 2520;
            const float deltaZ = synthetic ? volumeSize.z*volumeRadius/voxelRadius : (37.3082-32.1) /*/ 2*/; // Half pitch
            projections[index] = {vec3(-specimenDistance/volumeRadius,0, (float(index*stride)/float(projectionCount)*deltaZ)/volumeRadius - volumeAspectRatio + projectionAspectRatio), float(2*PI*float(index*stride)/num_projections_per_revolution)};
        }
        return projections;
    }

    buffer<Projection> projections;

    // Reconstruction
    string labels[1] = {"Adjoint"_};
    unique<Reconstruction> reconstructions[1] {unique<Approximate>(reconstructionSize, projections, projectionData, true, true, "small"_)};
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
    Window window {&layout, strx(projectionData.size)+" "_+strx(reconstructionSize) , int2(3*projectionView.sizeHint().x,projectionView.sizeHint().y+512)}; // FIXME

    Application(string path) : Poll(0,0,thread), projectionData(loadCDF(path)), projections(evaluateProjections(reconstructionSize, projectionData.size.xy(), projectionCount, stride, true)) { if(reconstructions[0]->k==-1) { queue(); thread.spawn(); } /*else view only*/ }
    void event() {
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        Reconstruction& r = reconstructions[index];
        //FIXME: keep every iterations
        if(existsFile(r.name, r.folder)) removeFile(r.name, r.folder);  // Removes previous evaluation
        rename(r.name+"."_+dec(r.k), r.name, r.folder); // Renames during step to invalidate volume if aborted
        r.step();
        const float PSNR = 10*log10( ::SSE(referenceVolume, r.x) / SSQ );
        plot[labels[index]].insert(r.totalTime.toFloat(), -PSNR);
        log("\t", r.totalTime.toFloat(), -PSNR);
        if(existsFile(r.name+"."_+dec(r.k), r.folder)) removeFile(r.name+"."_+dec(r.k), r.folder);  // Removes previous evaluation
        rename(r.name, r.name+"."_+dec(r.k), r.folder);
        window.render();
        /*if(r.k<4)*/ queue();
    }
} app ( arguments()[0] );
