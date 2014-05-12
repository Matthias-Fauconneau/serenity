#include "thread.h"
#include "cdf.h"
#include "view.h"
#include "project.h"
#include "layout.h"
#include "window.h"

VolumeCDF projectionData (Folder("Preprocessed"_ , Folder("Data"_, home())));
VolumeCDF reconstruction (Folder("FBP"_, Folder("Results"_, home())));

buffer<ImageF> images = sliceProjectionVolume(projectionData.volume);
buffer<Projection> projections = evaluateProjections(reconstruction.volume.sampleCount, images[0].size(), images.size);

ProjectionView projectionsView (images, 4);
//DiffView diff (&reconstruction.volume, &projections.volume);
VolumeView reconstructionView (reconstruction.volume, projections, 4);
HBox views ({ &projectionsView, /*&diff,*/ &reconstructionView });
//const int2 imageSize = int2(504, 378);
Window window (&views, "View"_);
