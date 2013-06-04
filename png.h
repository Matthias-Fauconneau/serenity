#pragma once
/// \file png.h PNG codec
#include "array.h"
#include "image.h"

/// Encodes image using the Portable Network Graphics standard
array<byte> encodePNG(const Image& image);
