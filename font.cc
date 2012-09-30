#include <ft2build.h>
#include <freetype2/freetype/freetype.h>
#include <freetype2/freetype/ftlcdfil.h>

#include "font.h"
#include "file.h"

const Folder& fonts() { static Folder folder = "usr/share/fonts"_; return folder; }

static FT_Library ft;
Font::Font(const File& file, int size) : keep(Map(file)) { load(keep,size); }
Font::Font(array<byte>&& data, int size):data(move(data)){ load(this->data,size); }
void Font::load(const ref<byte>& data, int size) {
    if(!ft) {
        FT_Init_FreeType(&ft);
        FT_Library_SetLcdFilter(ft,FT_LCD_FILTER_DEFAULT);
    }
    int e; if((e=FT_New_Memory_Face(ft,(const FT_Byte*)data.data,data.size,0,&face)) || !face) error("Couldn't load font",e,data.size);
    FT_Size_RequestRec req = {FT_SIZE_REQUEST_TYPE_REAL_DIM,size*64,size*64,0,0}; FT_Request_Size(face,&req);
    ascender=face->size->metrics.ascender*16/64;
}
void Font::setSize(int size) {
    if(fontSize==size) return; fontSize=size;
    FT_Size_RequestRec req = {FT_SIZE_REQUEST_TYPE_NOMINAL,size,size,0,0}; FT_Request_Size(face,&req);
    ascender=face->size->metrics.ascender*16/64;
}

uint16 Font::index(uint16 code) {
    for(int i=0;i<face->num_charmaps;i++) {
        FT_Set_Charmap(face, face->charmaps[i] );
        uint index = FT_Get_Char_Index(face, code);
        if(index) return index;
    }
    return code; //error("Unknown code",code);
}

int Font::kerning(uint16 leftIndex, uint16 rightIndex) { FT_Vector kerning; FT_Get_Kerning(face, leftIndex, rightIndex, FT_KERNING_DEFAULT, &kerning); return kerning.x*16/64; }

int Font::advance(uint16 index) { FT_Load_Glyph(face, index, FT_LOAD_TARGET_LCD); return face->glyph->advance.x*16/64; }
vec2 Font::size(uint16 index) { FT_Load_Glyph(face, index, FT_LOAD_TARGET_LCD); return vec2(face->glyph->metrics.width / 64.f, face->glyph->metrics.height / 64.f); }

const Glyph& Font::glyph(uint16 index, int) {
    // Lookup glyph in cache
    Glyph& glyph = cache[fontSize][index];
    if(glyph.valid) return glyph;
    glyph.valid=true;

    FT_Load_Glyph(face, index, FT_LOAD_TARGET_LCD);
    //FT_Set_Transform(face, 0, {fx,0});
    FT_Render_Glyph(face->glyph, FT_RENDER_MODE_LCD);
    glyph.offset = int2(face->glyph->bitmap_left, -face->glyph->bitmap_top);
    FT_Bitmap bitmap=face->glyph->bitmap;
    if(!bitmap.buffer) return glyph;
    int width = bitmap.width/3, height = bitmap.rows;
    Image image(width,height,true);
    for(int y=0;y<height;y++) for(int x=0;x<width;x++) {
        uint8* rgb = &bitmap.buffer[y*bitmap.pitch+x*3];
        image(x,y) = byte4(rgb[2],rgb[1],rgb[0],min(255,rgb[0]+rgb[1]+rgb[2]));
    }
    glyph.image = move(image);
    return glyph;
}
