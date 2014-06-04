#include "phantom.h"
#include "file.h"
#include "data.h"
#include "image.h"
#include "project.h"

struct Synthetic {
    Synthetic(string volumePath, string projectionPath) {
        Phantom phantom (16);
        const int3 volumeSize(64,64,64); //const int3 volumeSize(512,512,896);
        writeFile(volumePath+"."_+strx(volumeSize), cast<byte>(phantom.volume(volumeSize)));
        bool small = true;
        const uint stride=4, projectionCount = small ? 64*stride : 5041;
        const int3 size(small ? int2(64) : int2(504,378), projectionCount/stride);
        File file(projectionPath+"/"_+strx(size), currentWorkingDirectory(), Flags(ReadWrite|Create));
        file.resize(size.x*size.y*size.z*sizeof (float));
        Map map(file, Map::Prot(Map::Read|Map::Write));
        mref<float> data = mcast<float>((mref<byte>)map);
        buffer<Projection> projections = evaluateProjections(volumeSize, size.xy(), projectionCount, stride, small);
        for(uint z: range(size.z)) {
            log(z);
            ImageF target (buffer<float>(data.slice(z*size.y*size.x,size.y*size.x)), size.xy());
            phantom.project(target, projections[z]);
        }
    }
}
//app ("Results/synthetic.512x512x896"_,"Data/Synthetic/504x378x1260"_);
//app ("Results/synthetic"_,"Data/Synthetic"_);
//app ("Results/small"_,"Data/Small"_);
app ("Results/64"_,"Data/64"_);
