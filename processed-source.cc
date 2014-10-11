#include "processed-source.h"

SourceImageRGB ProcessedSource::image(size_t index) const {
    return cache<Image>(source.name(index), operation.name()+".sRGB", source.folder, [&](TargetImageRGB& target) {
        auto images = operation.apply(source.image(index, 0), source.image(index, 1), source.image(index, 2));
        sRGB(target.resize(size(index)), images[0], images[1], images[2]);
    }, time(index));
}
