#pragma once
/// \file png.h PNG codec
#include "image.h"

/// Encodes image in the Portable Network Graphics standard
buffer<byte> encodePNG(const Image& image);
