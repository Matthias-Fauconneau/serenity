#pragma once
/// \file jpeg-encoder.h JPEG encoder
#include "image.h"

/// Encodes image in the Joint Photographic Experts Group standard
buffer<byte> encodeJPEG(const Image& image, int quality = 100 /*50-95*/);
