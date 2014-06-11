#include "thread.h"
#include "view.h"
#include "project.h"
#include "layout.h"
#include "window.h"

const int N = fromInteger(arguments()[0]);
VolumeF volume (Map("data/"_+dec(N)+".ref"_));
SliceView sliceView (&volume, 1024/N);
//VolumeView volumeView (&volume, volume.size, 1024/N);
//HBox layout ({ &sliceView , &volumeView });
//Window window (&layout, dec(N));
Window window (&sliceView, dec(N));

double sum(const VolumeF& V) { double sum=0; for(uint z: range(V.size.z)) for(uint y: range(V.size.y)) for(uint x: range(V.size.x)) { float v = V(x,y,z); assert_(isNumber(v) && v>=0); sum+=v; } return sum; }

struct App { App() { double sum = ::sum(volume); log(sum / (volume.size.x*volume.size.y*volume.size.z)); } } app;
