#pragma once
#define RASTERIZED_PDF 0
#if RASTERIZED_PDF
#include "image.h"
buffer<byte> toPDF(const ref<Image>& pages);
#else
#include "vector.h"
#include "font.h"

struct PDFPage {
    int2 size;

    struct Character { Font& font; vec2 position; float size; uint code; };
    array<Character> characters;
};
buffer<byte> toPDF(const ref<PDFPage>& pages);
#endif
