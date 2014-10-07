#pragma once
/// \file jpeg.h JPEG decoder
#include "image.h"

/// Encodes image using the Portable Network Graphics standard
Image decodeJPEG(const ref<byte> file);
