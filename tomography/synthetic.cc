#include "phantom.h"
#include "file.h"
#include "data.h"
#include "image.h"
#include "volume.h"
#include "project.h"
#include "layout.h"
#include "window.h"
#include "view.h"

struct App {
    App() {
        const int N = fromInteger(arguments()[0]);
        int3 volumeSize = int3(N, N, N), projectionSize = int3(N, N, N);
        Phantom phantom (16);
        writeFile("ellipsoids/"_+strx(volumeSize)+".ref"_, cast<byte>(phantom.volume(volumeSize)));

        Map map(File("ellipsoids/"_+strx(projectionSize)+".proj"_, currentWorkingDirectory(), Flags(ReadWrite|Create)).resize(projectionSize.x*projectionSize.y*projectionSize.z*sizeof(float)), Map::Prot(Map::Read|Map::Write));
        mref<float> data = mcast<float>((mref<byte>)map);
        for(uint index: range(projectionSize.z)) {
            ImageF target (buffer<float>(data.slice(index*projectionSize.y*projectionSize.x,projectionSize.y*projectionSize.x)), projectionSize.xy());
            phantom.project(target, volumeSize, Projection(volumeSize, projectionSize, index));
        }
    }
} app;
