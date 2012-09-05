#pragma once
// This header exists to triggers compilation and linking of png.cc, PNG decoding is done using decodeImage in image.h
#include "array.h"
#include "image.h"
array<byte> encodePNG(const Image& image);
