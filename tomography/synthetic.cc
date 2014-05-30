#include "phantom.h"
#include "file.h"
#include "data.h"

struct Synthetic {
    Synthetic(string volumePath, string projectionPath) {
        Phantom phantom (16);
        const int3 volumeSize(128,128,256); //const int3 volumeSize(512,512,896);
        {
            File file(volumePath+"."_+strx(volumeSize), currentWorkingDirectory(), Flags(ReadWrite|Create));
            file.resize(volumeSize.x*volumeSize.y*volumeSize.z*sizeof(float));
            Map map(file, Map::Prot(Map::Read|Map::Write));
            VolumeF volume (volumeSize, buffer<float>(cast<float>(ref<byte>(map))));
            log("Volume", volumeSize);
            phantom.volume( volume );
        }
        {
#if 0
            const uint projectionCount = 5041, stride=4;
            const int3 size(504,378, projectionCount/stride);
#else // Synthetic
            const uint projectionCount = 256*4, stride=4;
            const int3 size(128,128, projectionCount/stride);
#endif
            File file(projectionPath+"/"_+strx(size), currentWorkingDirectory(), Flags(ReadWrite|Create));
            file.resize(size.x*size.y*size.z*sizeof (float));
            Map map(file, Map::Prot(Map::Read|Map::Write));
            VolumeF volume (size, buffer<float>(cast<float>(ref<byte>(map))));
            log("Projections", size);
            for(uint projectionIndex: range(size.z)) {
                log(projectionIndex);
                Projection projection(volumeSize, size.xy(), projectionIndex*stride, projectionCount);
                ImageF target = slice(volume, projectionIndex);
                phantom.project(target, projection);
            }
        }
    }
} //app ("Results/synthetic.512x512x896"_,"Data/Synthetic/504x378x1260"_);
app ("Results/synthetic"_,"Data/Synthetic"_);
