#pragma once
/// \file deflate.h DEFLATE codec (wraps public domain miniz v1.14 by Rich Geldreich)
#include "array.h"

array<byte> inflate(const ref<byte>& buffer, bool zlib=true);
array<byte> deflate(const ref<byte>& source, bool zlib=true);
