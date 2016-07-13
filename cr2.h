#pragma once
#include "image.h"

/// 2D array of 16bit integer samples
typedef ImageT<uint16> Image16;

Image16 decodeCR2(const ref<byte> file);
