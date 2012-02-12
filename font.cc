#include "font.h"
#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftlcdfil.h>

static FT_Library ft;
struct InitFreeType {
    InitFreeType() {
        FT_Init_FreeType(&ft);
        FT_Library_SetLcdFilter(ft,FT_LCD_FILTER_DEFAULT);
    }
} static_this;

Font::Font(const char* path) {
    FT_New_Face(ft, path, 0, &face);
    assert(face,strz(path));
}
Font::Font(string&& data) : data(move(data)) { FT_New_Memory_Face(ft,(const FT_Byte*)&data,data.size,0,&face); }

FontMetrics Font::metrics(int size) {
    FT_Size_RequestRec req = {FT_SIZE_REQUEST_TYPE_REAL_DIM,size<<6,size<<6,0,0};
    FT_Request_Size(face,&req);
    FontMetrics metrics = { face->size->metrics.descender>>6, face->size->metrics.ascender>>6, face->size->metrics.height>>6 };
    return metrics;
}

int Font::kerning(int leftCode, int rightCode) {
    int left = FT_Get_Char_Index(face, leftCode); assert(left,"glyph not found '"_,leftCode,'\'');
    int right = FT_Get_Char_Index(face, rightCode); assert(right,"glyph not found '"_,rightCode,'\'');
    FT_Vector kerning;
    FT_Get_Kerning(face, left, right, FT_KERNING_DEFAULT, &kerning );
    return kerning.x>>6;
}

GlyphMetrics Font::metrics(int size, int code) {
    assert(face);
    FT_Size_RequestRec req = {FT_SIZE_REQUEST_TYPE_REAL_DIM,size<<6,size<<6,0,0}; FT_Request_Size(face,&req);
    int index = FT_Get_Char_Index(face, code);
    assert(index); //if(!index) index=code;
    FT_Load_Glyph(face, index, FT_LOAD_TARGET_LCD);
    GlyphMetrics metrics={
    vec2(face->glyph->advance.x / 64.0, face->glyph->advance.y / 64.0),
    vec2(face->glyph->metrics.width / 64.0, face->glyph->metrics.height / 64.0)
    };
    return metrics;
}

Glyph& Font::glyph(int size, int code) {
    map<int, Glyph>& glyphs = cache[size];
    if(glyphs.values.size==0) glyphs.values.reserve(256); //realloc would invalid any references
    assert(glyphs.values.capacity==256);
    Glyph& glyph = glyphs[code]; //TODO: lookup for code in [0x20..0x80]
    if(glyph.texture || glyph.advance.x) return glyph;

    glyph.advance = metrics(size,code).advance;
    if(code == ' ') return glyph;
    FT_Render_Glyph(face->glyph, FT_RENDER_MODE_LCD);
    glyph.offset = vec2( face->glyph->bitmap_left, -face->glyph->bitmap_top );
    FT_Bitmap bitmap=face->glyph->bitmap;
    //assert(bitmap.buffer);
    if(!bitmap.buffer) return glyph;
    int width = bitmap.width/3, height = bitmap.rows;
    Image image(width,height);
    for(int y=0;y<height;y++) for(int x=0;x<width;x++) {
        uint8* rgb = &bitmap.buffer[y*bitmap.pitch+x*3];
        image.data[y*width+x] = byte4(255-rgb[0],255-rgb[1],255-rgb[2],255);
    }
    glyph.texture = image;
    return glyph;
}

Font defaultFont("/usr/share/fonts/dejavu/DejaVuSans.ttf");
