#pragma once
/// \file tiff.h Triggers dependency on tiff.cc to add support for TIFF decoding in decodeImage (weak link)
#include "core/image.h"

Image16 parseTIFF(buffer<byte>&& file);
