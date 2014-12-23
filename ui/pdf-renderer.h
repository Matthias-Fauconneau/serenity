#pragma once
/// \file pdf.h Portable Document Format renderer
#include "graphics.h"

buffer<Graphics> decodePDF(ref<byte> file, array<unique<Font>>& fonts);
