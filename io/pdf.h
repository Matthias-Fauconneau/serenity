#pragma once
#include "graphics.h"

buffer<byte> toPDF(int2 pageSize, const ref<Graphics> pages, float pointPx/*pt/px*/);
