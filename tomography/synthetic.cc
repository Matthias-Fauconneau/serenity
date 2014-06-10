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

const uint N = 128;
const bool simple = true; // otherwise same dimensions as MOQ

struct Synthetic {
    Synthetic(string volumePath, string projectionPath) {
        Phantom phantom (16);
        const int3 volumeSize = simple ? int3(N) : int3(512,512,896);
        writeFile(volumePath+"/"_+strx(volumeSize), cast<byte>(phantom.volume(volumeSize)));
        const uint stride = simple ? 1 : 4, projectionCount = simple ? N*stride : 5041;
        const int3 size(simple ? int2(N) : int2(504,378), projectionCount/stride);
        File file(projectionPath+"/"_+strx(size), currentWorkingDirectory(), Flags(ReadWrite|Create));
        file.resize(size.x*size.y*size.z*sizeof (float));
        Map map(file, Map::Prot(Map::Read|Map::Write));
        mref<float> data = mcast<float>((mref<byte>)map);
        buffer<Projection> projections = evaluateProjections(volumeSize, size.xy(), projectionCount, stride, simple);
        for(uint z: range(size.z)) {
            ImageF target (buffer<float>(data.slice(z*size.y*size.x,size.y*size.x)), size.xy());
            phantom.project(target, volumeSize, projections[z]);
        }
    }
} app ("Results/"_+dec(N),"Data/"_+dec(N));

struct Viewer {
    String data = "Data/"_+dec(N);
    const String projectionPath = data+"/"_+Folder(data).list()[0];
    VolumeF projectionData {parseSize(projectionPath), cast<float>((ref<byte>)Map(projectionPath))};
    const String volumePath = "Results/"_+dec(N)+"/"_+strx(int3(N));
    VolumeF volume {parseSize(volumePath), cast<float>((ref<byte>)Map(volumePath))};
    buffer<Projection> projections = evaluateProjections(volume.size, projectionData.size.xy(), projectionData.size.z, 1, true);
    Value index;
    SliceView sliceView {&projectionData, 4, index};
    VolumeView volumeView {&volume, projections, projectionData.size.xy(), 4, index};
    HBox layout {{&sliceView , &volumeView}};
    Window window {&layout, "View"_};
} viewer;
