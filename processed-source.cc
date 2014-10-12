#include "processed-source.h"

SourceImageRGB ProcessedSource::image(size_t index, int2 hint) const {
    return cache<Image>(name(index), operation.name()+".sRGB", source.folder, [&](TargetImageRGB& target) {
        Time time; log_(str(operation.name(), hint, ""));
        auto images = operation.apply(source.image(index, 0, hint), source.image(index, 1, hint), source.image(index, 2, hint));
        log(time);
        sRGB(target.resize(images[0].size), images[0], images[1], images[2]);
    }, time(index), hint);
}
