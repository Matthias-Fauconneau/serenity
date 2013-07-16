#pragma once
/// \file bmp.h BMP codec
#include "image.h"

/// Wraps image using Windows device independent bitmap (DIB) headers
/// \note Exports only grayscale images
buffer<byte> encodeBMP(const Image& image);
