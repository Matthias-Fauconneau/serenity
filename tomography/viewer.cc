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

struct Viewer {
    Map file;
    VolumeF volume;
    buffer<Projection> projections = evaluateProjections(volume.sampleCount, int2(504,378), 5041);
    SliceView sliceView {volume, 2};
    VolumeView volumeView {volume, projections, 2};
    HBox views {{ &sliceView, &volumeView }};
    Window window;

    Viewer(string path) :
        file(path),
        volume (parseSize(path), cast<float>((ref<byte>)file)),
        window (&views, section(section(path,'/',-2,-1),'.')) {
        assert_(volume.data.size != volume.size()*sizeof(float));
    }
} app ( arguments()[0] );

#endif
