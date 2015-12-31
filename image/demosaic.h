#pragma once
#include "image.h"

inline Image4f demosaic(const ImageF& source) {
    Image4f target (source.size/2);
    for(size_t y: range(target.height)) for(size_t x: range(target.width)) {
        target(x, y) = {source(x*2+1, y*2+0), (source(x*2+0, y*2+0)+source(x*2+1, y*2+1))/2, source(x*2+0, y*2+1)};
    }
    return target;
}
