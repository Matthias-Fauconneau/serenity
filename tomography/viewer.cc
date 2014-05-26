#include "thread.h"
#include "cdf.h"
#include "view.h"
#include "project.h"
#include "layout.h"
#include "window.h"

#if 0
VolumeCDF projectionData (Folder("Preprocessed"_ , Folder("Data"_, home())));
VolumeCDF reconstruction (Folder("FBP"_, Folder("Results"_, home())));

buffer<ImageF> images = sliceProjectionVolume(projectionData.volume);
buffer<Projection> projections = evaluateProjections(reconstruction.volume.sampleCount, images[0].size(), images.size);

ProjectionView projectionsView (images, 2);
//DiffView diff (&reconstruction.volume, &projections.volume);
VolumeView reconstructionView (reconstruction.volume, projections, 2);
HBox views ({ &projectionsView, /*&diff,*/ &reconstructionView });
//const int2 imageSize = int2(504, 378);
Window window (&views, "View"_);
#else

int3 parseSize(string path) {
    TextData s (section(path,'/',-2,-1));
    s.until('.');
    int X = s.mayInteger(); if(!s.match("x"_)) error(s.buffer);
    int Y = s.mayInteger(); if(!s.match("x"_)) error(s.buffer);
    int Z = s.mayInteger();
    return int3(X,Y,Z);
}

struct Viewer : HBox {
    buffer<Map> files;
    buffer<VolumeF> volumes;
    uint currentIndex = 0;
    const VolumeF* current() { return &volumes[currentIndex]; }
    buffer<Projection> projections = evaluateProjections(current()->sampleCount, int2(504,378), 5041);
    SliceView sliceView {current(), 2};
    VolumeView volumeView {current(), projections, 2};
    Window window;

    Viewer(ref<string> paths) :
        HBox{{ &sliceView, &volumeView }},
        files( apply(paths,[](string path){ return Map(path); } ) ),
        volumes ( apply(paths.size, [&](uint i){ return VolumeF(parseSize(paths[i]), cast<float>((ref<byte>)files[i])); }) ),
        window (this, join(apply(paths,[](string path){ return section(section(path,'/',-2,-1),'.'); })," "_)) {
    }
    bool mouseEvent(const Image& target, int2 cursor, int2 size, Event event, Button button) override {
        if((button == WheelUp &&  currentIndex> 0) || (button == WheelDown && currentIndex < volumes.size-1)) {
            if(button == WheelUp) currentIndex--; else currentIndex++;
            sliceView.volume = current();
            volumeView.volume = current();
            return true;
        }
        return HBox::mouseEvent(target, cursor, size, event, button);
    }
} app ( arguments() );

#endif
