#include "thread.h"
#include "sum.h"
#include "project.h"
#include "view.h"
#include "layout.h"
#include "window.h"

const int N = fromInteger(arguments()[0]);
CLVolume volume (Map("data/"_+dec(N)+".ref"_));

struct App {
    App() {
        double sum = ::sum(volume); log(sum / (volume.size.x*volume.size.y*volume.size.z));
        double SSQ = ::SSQ(volume); log(sqrt(SSQ / (volume.size.x*volume.size.y*volume.size.z)));
    }
} app;

SliceView sliceView (volume, 1024/N);
#if PRECOMPUTE
VolumeF projectionData (Map(File("data/"_+dec(N)+".proj"_,currentWorkingDirectory(),Flags(ReadWrite|Create|Truncate)).resize(cb(N)*sizeof(float)), Map::Prot(Map::Read|Map::Write)));
SliceView volumeView (&projectionData, 1024/N, VolumeView::staticIndex);
struct App {
    App() {
        project(projectionData, volume);
    }
} app;
#else
VolumeView volumeView (volume, volume.size, 1024/N);
#endif
HBox layout ({ &sliceView , &volumeView });
Window window (&layout, dec(N));