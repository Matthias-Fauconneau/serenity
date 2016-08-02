#pragma once
/// \file jpeg.h JPEG decoder
#include "image.h"

/// Decodes image from the Joint Photographic Experts Group standard
Image decodeJPEG(const ref<byte> file);

/// Encodes image in the Joint Photographic Experts Group standard
buffer<byte> encodeJPEG(const Image& image, int quality = 100 /*50-95*/);
