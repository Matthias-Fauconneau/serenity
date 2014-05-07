#include "project.h"
#include "thread.h"

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& source, const Projection& projection) {
    const CylinderVolume volume = source;
    parallel(image.height, [&projection, &volume, &source, &image](uint, uint y) {
        v4sf start,end;
        mref<float> row = image.data.slice(y*image.width, image.width);
        for(uint x: range(row.size)) row[x] = intersect(projection, vec2(x, y), volume, start, end) ? project(start, projection.ray, end, volume, source.data) : 0;
    }, coreCount);
}
