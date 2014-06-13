#include "thread.h"
#include "operators.h"
#include "project.h"
#include "view.h"
#include "layout.h"
#include "window.h"

const uint N = fromInteger(arguments()[0]);
const int3 projectionSize = int3(N);
const int3 volumeSize = int3(2*N);

const VolumeF& cylinder(const VolumeF& target) {
    int3 size = target.size;
    const float2 center = float2(size.xy()-int2(1))/2.f;
    const float radiusSq = sq(center.x);
    for(uint z: range(size.z)) for(uint y: range(size.y)) for(uint x: range(size.x)) target(x,y,z) = sq(float2(x,y)-center)<=radiusSq;
    return target;
}

CLVolume x (Map(strx(volumeSize)+".ref"_));
//CLVolume x = cylinder(volumeSize);

VolumeF Ax (Map(File(strx(projectionSize)+".proj"_,currentWorkingDirectory(),Flags(ReadWrite|Create|Truncate)).resize(cb(N)*sizeof(float)), Map::Prot(Map::Read|Map::Write)));

struct App {
    App() {
        for(uint index: range(Ax.size.z)) {
            log(index);
            const bool oversample = false;
            if(oversample) {
                ImageF fullSize(2*projectionSize.xy());
                ::project(fullSize, x, Ax.size.z, index);
                downsample(slice(Ax, index), fullSize);
            } else {
                ::project(slice(Ax, index), x, Ax.size.z, index);
            }
        }
        double sum = ::sum(x); log(sum / (volumeSize.x*volumeSize.y*volumeSize.z));
        double SSQ = ::SSQ(x); log(sqrt(SSQ / (volumeSize.x*volumeSize.y*volumeSize.z)));
    }
} app;

SliceView sliceView (x, 1024/volumeSize.x);
SliceView volumeView (Ax, 1024/projectionSize.x, VolumeView::staticIndex);
HBox layout ({ &sliceView , &volumeView });
Window window (&layout, strx(volumeSize));
