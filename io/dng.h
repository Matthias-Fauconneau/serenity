#pragma once
#include "core/image.h"

struct DNG : Image16 {
    uint16 blackLevel = -1;
    using Image16::Image16;
};

DNG parseDNG(ref<byte> file, bool decode=true);
