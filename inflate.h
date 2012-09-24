#pragma once
/// \file inflate.h DEFLATE standard decoder (wrap Rich Geldreich's tinflate.cc)
#include "array.h"

array<byte> inflate(const ref<byte>& buffer, bool zlib);
