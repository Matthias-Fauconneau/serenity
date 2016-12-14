#pragma once
/// \file deflate.h DEFLATE codec (wraps public domain miniz v1.14 by Rich Geldreich)
#include "memory.h"

buffer<byte> inflate(const ref<byte> buffer, bool zlib=true);
buffer<byte> deflate(const ref<byte> source, bool zlib=true);
