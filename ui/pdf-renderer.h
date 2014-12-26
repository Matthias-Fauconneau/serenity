#pragma once
/// \file pdf.h Portable Document Format renderer
#include "graphics.h"

/// Converts a Portable Document Format file to \a Graphics pages
buffer<Graphics> decodePDF(ref<byte> file, array<unique<FontData>>& fonts);
