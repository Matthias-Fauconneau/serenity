#pragma once
#include "core/image.h"

struct RAW : Image16 {
    uint16 blackLevel = -1;
    using Image16::Image16;
};

RAW parseTIF(ref<byte> file);
