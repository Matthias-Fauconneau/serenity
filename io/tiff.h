#pragma once
/// \file tiff.h Triggers dependency on tiff.cc to add support for TIFF decoding in decodeImage (weak link)
#include "image.h"

Image16 parseTIFF(const ref<byte> file);
