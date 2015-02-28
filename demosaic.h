#pragma once
#include "image.h"

ImageF fromRaw16(ref<uint16> source, int2 size) {
    ImageF target (size);
    assert_(target.Ref::size == source.size);
    for(size_t i: range(source.size)) target[i] = (float) source[i] / ((1<<16)-1);
    return target;
}

Image4f demosaic(const ImageF& source) {
    Image4f target (source.size/2);
    for(size_t y: range(target.height)) for(size_t x: range(target.width)) {
        target(x, y) = {source(x*2+1, y*2+0), (source(x*2+0, y*2+0)+source(x*2+1, y*2+1))/2, source(x*2+0, y*2+1)};
    }
    return target;
}
