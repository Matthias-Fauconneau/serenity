#include "thread.h"
#include "volume.h"

/// Downsamples a volume by averaging 2x2x2 samples
void downsample(const VolumeF& target, const VolumeF& source) {
    assert_(source.size.x%2==0 && source.size.y%2==0 && source.size.z%2==0);
    assert_(target.data.size == source.data.size/2/2/2);
    for(uint z: range(source.size.z/2)) for(uint y: range(source.size.y/2)) for(uint x: range(source.size.x/2))
        target(x,y,z) = (source(x*2,y*2,z*2) + source(x*2+1,y*2,z*2) + source(x*2,y*2+1,z*2) + source(x*2+1,y*2+1,z*2) + source(x*2,y*2,z*2+1) + source(x*2+1,y*2,z*2+1) + source(x*2,y*2+1,z*2+1) + source(x*2+1,y*2+1,z*2+1)) / 8;
}

struct App {
    App() {
        const int N = fromInteger(arguments()[0]);
        //downsample(VolumeF(Map(File(strx(int3(N))+".ref"_,currentWorkingDirectory(),Flags(ReadWrite|Create|Truncate)).resize(cb(N)*sizeof(float)), Map::Prot(Map::Read|Map::Write))), VolumeF(Map(strx(int3(N*2))+".ref"_)));
        cylinder(VolumeF(Map(File("cylinder."_+strx(int3(N))+".ref"_,currentWorkingDirectory(),Flags(ReadWrite|Create|Truncate)).resize(cb(N)*sizeof(float)), Map::Prot(Map::Read|Map::Write))));
    }
} app;
