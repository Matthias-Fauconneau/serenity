#pragma once
#include "graphics.h"

/// \arg pageSize Page size in pixels
/// pointPx points per pixels
buffer<byte> toPDF(int2 pageSize, const ref<Graphics> pages, float pointPx = 1/*pt/px*/);
