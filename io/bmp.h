#pragma once
/// \file bmp.h BMP codec
#include "image.h"

/// Wraps image using Windows device independent bitmap (DIB) headers
buffer<byte> encodeBMP(const Image& image);
