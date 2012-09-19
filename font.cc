#include <ft2build.h>
#include <freetype2/freetype/freetype.h>
#include <freetype2/freetype/ftlcdfil.h>
#undef offsetof

#include "font.h"
#include "file.h"

static const Folder& fonts() { static Folder folder = "usr/share/fonts"_; return folder; }

static FT_Library ft;
Font::Font(const ref<byte>& path, int size) : file(Map(path,fonts())) {
    if(!ft) {
        FT_Init_FreeType(&ft);
        FT_Library_SetLcdFilter(ft,FT_LCD_FILTER_DEFAULT);
    }
    FT_New_Memory_Face(ft,(const FT_Byte*)file.data,file.size,0,&face);
    FT_Size_RequestRec req = {FT_SIZE_REQUEST_TYPE_REAL_DIM,size<<6,size<<6,0,0}; FT_Request_Size(face,&req);
    //FontMetrics metrics = { face->size->metrics.descender/64.f, face->size->metrics.ascender/64.f, face->size->metrics.height/64.f };
    descender=face->size->metrics.descender*16/64; ascender=face->size->metrics.ascender*16/64; height=face->size->metrics.height*16/64;
    assert(face,path);
}

uint16 Font::index(uint16 code) { uint index = FT_Get_Char_Index(face, code); assert(index,"index not found '"_+hex(code)+"'"_); return index; }

int Font::kerning(uint16 leftIndex, uint16 rightIndex) { FT_Vector kerning; FT_Get_Kerning(face, leftIndex, rightIndex, FT_KERNING_DEFAULT, &kerning); return kerning.x*16/64; }

int Font::advance(uint16 index) { FT_Load_Glyph(face, index, FT_LOAD_TARGET_LCD); return face->glyph->advance.x*16/64; }

const Glyph& Font::glyph(uint16 index, int) {
    // Lookup glyph in cache
    Glyph& glyph = index<128 ? cacheASCII[index] : cacheUnicode[index];
    if(glyph.valid) return glyph;
    glyph.valid=true;

    FT_Load_Glyph(face, index, FT_LOAD_TARGET_LCD);
    //FT_Set_Transform(face, 0, {fx,0});
    FT_Render_Glyph(face->glyph, FT_RENDER_MODE_LCD);
    glyph.offset = int2(face->glyph->bitmap_left, -face->glyph->bitmap_top);
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
