#pragma once
/// \file jpeg.h JPEG decoder
#include "core/image.h"

/// Decodes image from the Joint Photographic Experts Group standard
Image decodeJPEG(const ref<byte> file);
