#include "phantom.h"
#include "file.h"
#include "data.h"

struct Synthetic {
    Synthetic(string volumePath, string projectionPath) {
        Phantom phantom;
        const int3 volumeSize(512,512,896);
        {
            File file(volumePath, currentWorkingDirectory(), Flags(ReadWrite|Create));
            file.resize(volumeSize.x*volumeSize.y*volumeSize.z*sizeof(float));
            Map map(file, Map::Prot(Map::Read|Map::Write));
            VolumeF volume (volumeSize, buffer<float>(cast<float>(ref<byte>(map))));
            log("Volume", volumeSize);
            phantom.volume( volume );
        }
        {
            File file(projectionPath, currentWorkingDirectory(), Flags(ReadWrite|Create));
            const uint projectionCount = 5041, stride=4;
            const int3 size(504,378, projectionCount/stride);
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
} app ("Results/synthetic.512x512x896"_,"Data/Synthetic/504x378x1260"_);
