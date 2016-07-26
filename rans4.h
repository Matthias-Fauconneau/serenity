#pragma once
#include "image.h"

void decodeRANS4(const Image16& target, const ref<byte> source);
size_t encodeRANS4(const mref<byte> target, const Image16& source);
