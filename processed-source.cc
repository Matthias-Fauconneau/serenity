#include "processed-source.h"

SourceImageRGB ProcessedSource::image(size_t index, int2 size, bool ignoreCache) const {
    return cache<Image>(source.folder, operation.name()+".sRGB", name(index), size, ignoreCache ? realTime() : time(index),
                 [&](TargetImageRGB& target) {
        Time time; log_(str(operation.name(), size, ""));
        auto images = operation.apply(source.image(index, 0, size), source.image(index, 1, size), source.image(index, 2, size));
        log(time);
        sRGB(target.resize(images[0].size), images[0], images[1], images[2]);
    });
}
