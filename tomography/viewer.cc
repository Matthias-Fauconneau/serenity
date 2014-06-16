#include "thread.h"
#include "operators.h"
#include "project.h"
#include "view.h"
#include "layout.h"
#include "window.h"

const uint N = fromInteger(arguments()[0]);
const int3 size = int3(N, N, N);
VolumeF hostVolume = Map("cylinder"_+"."_+strx(size)+".ref"_);
CLVolume volume (hostVolume);

struct App {
    App() {
        double sum = ::sum(volume); log(sum / (size.x*size.y*size.z));
        double SSQ = ::SSQ(volume); log(sqrt(SSQ / (size.x*size.y*size.z)));
    }
} app;

SliceView sliceView (volume, 512/N);
VolumeView volumeView (volume, volume.size, 512/N);
HBox layout ({ &sliceView , &volumeView });
Window window (&layout, strx(size));
