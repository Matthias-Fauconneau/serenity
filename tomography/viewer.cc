#include "thread.h"
#include "cdf.h"
#include "view.h"
#include "project.h"
#include "layout.h"
#include "window.h"


VolumeCDF reconstruction (Folder("FBP"_, Folder("Results"_, home())));
VolumeCDF projections (Folder("Preprocessed"_ /*"LIN_PSS"_*/, Folder("Data"_, home())));
View reconstructionView (&reconstruction.volume, true);
DiffView diff (&reconstruction.volume, &projections.volume);
View projectionsView (&projections.volume, false);
HBox views ({ &reconstructionView, &diff, &projectionsView });
Window window (&views, "View"_, int2(3*sensorSize.x,sensorSize.y));
