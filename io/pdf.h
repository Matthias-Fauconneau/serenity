#pragma once
#define RASTERIZED_PDF 0
#if RASTERIZED_PDF
#include "image.h"
buffer<byte> toPDF(const ref<Image>& pages);
#else
#include "widget.h"

buffer<byte> toPDF(int2 pageSize, const ref<Graphics>& pages);
#endif
