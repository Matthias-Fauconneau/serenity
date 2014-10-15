#include "processed-source.h"

SourceImageRGB ProcessedSource::image(size_t index, int2 size, bool ignoreCache) const {
    return cache<Image>(source.folder, operation.name(), name(index), size?:this->size(index), ignoreCache ? realTime() : time(index),
                 [&](const Image& target) {
        auto images = operation.apply(source.image(index, 0, size), source.image(index, 1, size), source.image(index, 2, size));
        sRGB(target, images[0], images[1], images[2]);
    });
}
