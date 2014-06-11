#include "thread.h"
#include "vector.h"

struct VolumeF {
    VolumeF(int3 size, const ref<float>& data) : size(size), data(data) { assert_(data.size == (size_t)size.x*size.y*size.z); }
    VolumeF(const ref<float>& data) : VolumeF(round(pow(data.size,1./3)), data) {}
    inline float& operator()(uint x, uint y, uint z) const {assert_(x<size.x && y<size.y && z<size.z, x,y,z, size); return data[z*size.y*size.x+y*size.x+x]; }

    uint3 size = 0;
    buffer<float> data;
};

/// Downsamples a volume by averaging 2x2x2 samples
void downsample(const VolumeF& target, const VolumeF& source) {
    assert_(source.size.x%2==0 && source.size.y%2==0 && source.size.z%2==0);
    assert_(target.data.size == source.data.size/2/2/2);
    for(uint z: range(source.size.z/2)) for(uint y: range(source.size.y/2)) for(uint x: range(source.size.x/2))
        target(x,y,z) = (source(x*2,y*2,z*2) + source(x*2+1,y*2,z*2) + source(x*2,y*2+1,z*2) + source(x*2+1,y*2+1,z*2) + source(x*2,y*2,z*2+1) + source(x*2+1,y*2,z*2+1) + source(x*2,y*2+1,z*2+1) + source(x*2+1,y*2+1,z*2+1)) / 8;
}

void __attribute((constructor)) script() {
    //downsample(VolumeF(Map(File("data/256.ref"_,currentWorkingDirectory(),Flags(ReadWrite|Create|Truncate)).resize(cb(256)*sizeof(float)), Map::Prot(Map::Read|Map::Write))), VolumeF(Map("data/512.ref"_)));
    //downsample(VolumeF(Map(File("data/128.ref"_,currentWorkingDirectory(),Flags(ReadWrite|Create|Truncate)).resize(cb(128)*sizeof(float)), Map::Prot(Map::Read|Map::Write))), VolumeF(Map("data/256.ref"_)));
    downsample(VolumeF(Map(File("data/64.ref"_,currentWorkingDirectory(),Flags(ReadWrite|Create|Truncate)).resize(cb(64)*sizeof(float)), Map::Prot(Map::Read|Map::Write))), VolumeF(Map("data/128.ref"_)));
}
