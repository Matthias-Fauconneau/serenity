#pragma once
/// \file jpeg.h JPEG decoder
#include "image.h"

/// Decodes image from the Joint Photographic Experts Group standard
Image decodeJPEG(const ref<byte> file);
