#include "thread.h"
#include "operators.h"
#include "project.h"
#include "view.h"
#include "layout.h"
#include "window.h"

const uint N = fromInteger(arguments()[0]);
const int3 size = int3(N, N, N);
CLVolume volume (Map(strx(size)+".ref"_));

struct App {
    App() {
        double sum = ::sum(volume); log(sum / (size.x*size.y*size.z));
        double SSQ = ::SSQ(volume); log(sqrt(SSQ / (size.x*size.y*size.z)));
    }
} app;

SliceView sliceView (volume, 1024/N);
VolumeView volumeView (volume, volume.size, 1024/N);
HBox layout ({ &sliceView , &volumeView });
Window window (&layout, strx(size));
