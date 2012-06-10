#include "font.h"
#include "file.h"

#undef abort
#undef exit
#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftlcdfil.h>

#include "array.cc"
ArrayOfDefaultConstructible(Glyph)
ArrayOfDefaultConstructible(Font::GlyphCache)

static FT_Library ft;
struct InitFreeType {
    InitFreeType() {
        FT_Init_FreeType(&ft);
        FT_Library_SetLcdFilter(ft,FT_LCD_FILTER_DEFAULT);
    }
} static_this;

Font::Font(string name) {
    FT_New_Face(ft, strz(findFile("/usr/share/fonts"_,name)).data(), 0, &face);
    assert(face,name);
}
Font::Font(array<byte>&& data) : data(move(data)) { FT_New_Memory_Face(ft,(const FT_Byte*)data.data(),data.size(),0,&face); }

FontMetrics Font::metrics(int size) {
    FT_Size_RequestRec req = {FT_SIZE_REQUEST_TYPE_REAL_DIM,size<<6,0,0,0};
    FT_Request_Size(face,&req);
    FontMetrics metrics = { face->size->metrics.descender/64.f, face->size->metrics.ascender/64.f, face->size->metrics.height/64.f };
    return metrics;
}

float Font::kerning(int leftCode, int rightCode) {
    int left = FT_Get_Char_Index(face, leftCode); assert(left,"glyph not found '"_+hex(leftCode)+"'"_);
    int right = FT_Get_Char_Index(face, rightCode); assert(right,"glyph not found '"_+hex(rightCode)+"'"_);
    FT_Vector kerning;
    FT_Get_Kerning(face, left, right, FT_KERNING_DEFAULT, &kerning );
    return kerning.x/64.f;
}

GlyphMetrics Font::metrics(int size, int code) {
    assert(face);
    FT_Size_RequestRec req = {FT_SIZE_REQUEST_TYPE_REAL_DIM,size<<6,size<<6,0,0}; FT_Request_Size(face,&req);
    int index = FT_Get_Char_Index(face, code);
    assert(index, hex(code)); //if(!index) index=code;
    FT_Load_Glyph(face, index, FT_LOAD_TARGET_LCD);
    GlyphMetrics metrics={
    vec2(face->glyph->advance.x / 64.f, face->glyph->advance.y / 64.f),
    vec2(face->glyph->metrics.width / 64.f, face->glyph->metrics.height / 64.f)
    };
    return metrics;
}

Glyph& Font::glyph(int size, int code) {
    GlyphCache& glyphs = cache[size];
    if(!glyphs.values) glyphs.values.reserve(256); //FIXME: dynamic array would invalid any references
    assert(glyphs.values.capacity()==256);
    Glyph& glyph = glyphs[code]; //TODO: lookup for code in [0x20..0x80]
    if(glyph.image || glyph.advance.x) return glyph;

    glyph.advance = metrics(size,code).advance;
    if(code == ' ') return glyph;
    FT_Render_Glyph(face->glyph, FT_RENDER_MODE_LCD);
    glyph.offset = vec2( face->glyph->bitmap_left, -face->glyph->bitmap_top );
    FT_Bitmap bitmap=face->glyph->bitmap;
    if(!bitmap.buffer) return glyph;
    int width = bitmap.width/3, height = bitmap.rows;
    Image image(width,height);
    for(int y=0;y<height;y++) for(int x=0;x<width;x++) {
        uint8* rgb = &bitmap.buffer[y*bitmap.pitch+x*3];
        image(x,y) = byte4(255-rgb[2],255-rgb[1],255-rgb[0],rgb[0]|rgb[1]|rgb[2]?255:0);
    }
    glyph.image = Image(width,height);
    for(int y=0;y<height;y++) for(int x=0;x<width;x++) { //feather alpha
        byte4& d = glyph.image(x,y);
        d = image(x,y);
        if(d.a==0) d.a=(
                    image.get(x-1,y-1).a+image.get(x ,y - 1).a+image.get(x+1,y -1).a+
                    image.get(x-1,y+0).a+image.get(x,y     ).a+image.get(x+1,y   ).a+
                    image.get(x-1,y+1).a+image.get(x,y+1).a+image.get(x+1,y+1).a ) / 8;
    }
    return glyph;
}

Font defaultSans("DejaVuSans.ttf"_);
Font defaultBold("DejaVuSans-Bold.ttf"_);
Font defaultItalic("DejaVuSans-Oblique.ttf"_);
Font defaultBoldItalic("DejaVuSans-BoldOblique.ttf"_);
