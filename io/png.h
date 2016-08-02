#pragma once
/// \file png.h PNG codec
#include "image.h"

/// Decodes image from the Portable Network Graphics standard
Image decodePNG(const ref<byte> file);

/// Encodes image in the Portable Network Graphics standard
buffer<byte> encodePNG(const Image& image);
