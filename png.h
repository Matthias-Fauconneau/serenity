#pragma once
/// \file png.h PNG decoder and uncompressed encoder
#include "array.h"
#include "image.h"
array<byte> encodePNG(const Image& image);
