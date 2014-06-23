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
        const int3 N = fromInteger(arguments()[0]);
        File targetFile(strx(int3(N))+".ref"_,"Data"_,Flags(ReadWrite|Create|Truncate));
        targetFile.resize(N.x*N.y*N.z*sizeof(float));
        VolumeF target (N, Map(targetFile, Map::Prot(Map::Read|Map::Write)));
        VolumeF source(N*2, Map(strx(int3(N*2))+".ref"_, "Data"_));
        downsample(target, source);
        target = normalize(move(target));
    }
} app;
