#include "phantom.h"
#include "file.h"
#include "data.h"
#include "image.h"
#include "volume.h"
#include "project.h"
#include "layout.h"
#include "window.h"
#include "view.h"
#include "cdf.h"

struct Synthetic {
    Synthetic(string volumePath, string projectionPath) {
        const bool small = true;
        Phantom phantom (16);
        const int3 volumeSize = small ? int3(64) : int3(512,512,896);
        writeFile(volumePath+"."_+strx(volumeSize), cast<byte>(phantom.volume(volumeSize)));
        const uint stride = small ? 1 : 4, projectionCount = small ? 64*stride : 5041;
        const int3 size(small ? int2(64) : int2(504,378), projectionCount/stride);
        File file(projectionPath+"/"_+strx(size), currentWorkingDirectory(), Flags(ReadWrite|Create));
        file.resize(size.x*size.y*size.z*sizeof (float));
        Map map(file, Map::Prot(Map::Read|Map::Write));
        mref<float> data = mcast<float>((mref<byte>)map);
        buffer<Projection> projections = evaluateProjections(volumeSize, size.xy(), projectionCount, stride, small);
        for(uint z: range(size.z)) {
            ImageF target (buffer<float>(data.slice(z*size.y*size.x,size.y*size.x)), size.xy());
            phantom.project(target, projections[z]);
        }
    }
} app ("Results/64"_,"Data/64"_);

struct Viewer {
    const string projectionPath = "Data/64/64x64x64"_;
    VolumeF projectionData {parseSize(projectionPath), cast<float>((ref<byte>)Map(projectionPath))};
    const string volumePath = "Results/64.64x64x64"_;
    VolumeF volume {parseSize(volumePath), cast<float>((ref<byte>)Map(volumePath))};
    buffer<Projection> projections = evaluateProjections(volume.size, projectionData.size.xy(), projectionData.size.z, 1, true);
    SliceView sliceView {&projectionData, 4};
    VolumeView volumeView {&volume, projections, projectionData.size.xy(), 4};
    HBox layout {{&sliceView , &volumeView}};
    Window window {&layout, "View"_};
} viewer;
