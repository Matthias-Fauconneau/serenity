#pragma once
#include "image.h"

#define RASTERIZED_PDF 1
#if RASTERIZED_PDF
buffer<byte> toPDF(const ref<Image>& images);
#endif
