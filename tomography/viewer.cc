#include "thread.h"
#include "operators.h"
#include "project.h"
#include "view.h"
#include "layout.h"
#include "window.h"

const uint N = fromInteger(arguments()[0]);
const int3 size = N;
CLVolume volume (size, Map(strx(size)+".ref"_, "Data"_));

struct App {
    App() {
        double sum = ::sum(volume); log(sum, sum / (size.x*size.y*size.z));
        double SSQ = ::SSQ(volume); log(sqrt(SSQ), sqrt(SSQ / (size.x*size.y*size.z)));
    }
} app;

SliceView sliceView (volume, 512/N);
Projection A(volume.size, volume.size);
VolumeView volumeView (volume, A, 512/N);
HBox layout ({ &sliceView , &volumeView });
Window window (&layout, strx(size));
