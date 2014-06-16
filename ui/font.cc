#include <ft2build.h>
#include <freetype2/freetype.h> //freetype
#include <freetype2/ftlcdfil.h>

#include "font.h"
#include "file.h"

static FT_Library ft; static int fontCount=0;
Font::Font(const ref<byte>& data, int size) {
    if(!ft) {
        FT_Init_FreeType(&ft);
        FT_Library_SetLcdFilter(ft,FT_LCD_FILTER_DEFAULT);
    }
    int e; if((e=FT_New_Memory_Face(ft,(const FT_Byte*)data.data,data.size,0,&face)) || !face) { error("Invalid font"); return; }
    fontCount++;
    FT_Size_RequestRec req = {FT_SIZE_REQUEST_TYPE_REAL_DIM,size*64,size*64,0,0}; FT_Request_Size(face,&req);
    ascender=((FT_FaceRec*)face)->size->metrics.ascender*0x1p-6;
}
Font::Font(::buffer<byte>&& data, int size) : Font(data, size) { buffer = move(data); }
Font::Font(Map&& data, int size) : Font(data, size) { map = move(data); }
Font Font::byName(string name, int size) { for(Folder folder: {"/usr/share/fonts"_,""_}) for(string path: folder.list(Files|Recursive)) if(find(path,name)) return Font(File(path, folder), size); error("No such font",name); }

Font::~Font(){
    if(face) {
        FT_Done_Face(face); face=0; fontCount--;
        assert(fontCount>=0); if(fontCount == 0) { assert(ft); FT_Done_FreeType(ft), ft=0; }
    }
}


uint16 Font::index(const string& name) {
    uint index = FT_Get_Name_Index(face, (char*)(const char*)strz(name));
    if(!index) for(int i=0;i<face->num_glyphs;i++) { char buffer[256]; FT_Get_Glyph_Name(face,i,buffer,sizeof(buffer)); log(buffer); }
    assert(index,name); return index;
}
uint16 Font::index(uint16 code) {
    for(int i=0;i<face->num_charmaps;i++) {
        FT_Set_Charmap(face, face->charmaps[i] );
        uint index = FT_Get_Char_Index(face, code);
        if(index) return index;
    }
    return code;
}

float Font::kerning(uint16 leftIndex, uint16 rightIndex) {
    FT_Vector kerning; FT_Get_Kerning(face, leftIndex, rightIndex, FT_KERNING_DEFAULT, &kerning); return kerning.x*0x1p-6;
}
float Font::advance(uint16 index) { Locker locker(lock); FT_Load_Glyph(face, index, FT_LOAD_TARGET_NORMAL); return face->glyph->advance.x*0x1p-6; }
float Font::linearAdvance(uint16 index) { Locker locker(lock); FT_Load_Glyph(face, index, FT_LOAD_TARGET_NORMAL); return face->glyph->linearHoriAdvance*0x1p-16; }
vec2 Font::size(uint16 index) { Locker locker(lock); FT_Load_Glyph(face, index, FT_LOAD_TARGET_NORMAL); return vec2(face->glyph->metrics.width*0x1p-6, face->glyph->metrics.height*0x1p-6); }

const Glyph& Font::glyph(uint16 index, int) {
    Glyph& glyph = cache[uint(fontSize)][index];
    if(glyph.valid) return glyph;
    glyph.valid=true;

    Locker locker(lock);
    FT_Load_Glyph(face, index, FT_LOAD_TARGET_NORMAL);
    FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
    glyph.offset = int2(face->glyph->bitmap_left, -face->glyph->bitmap_top);
    FT_Bitmap bitmap=face->glyph->bitmap;
    if(!bitmap.buffer) return glyph;
    int width = bitmap.width, height = bitmap.rows;
    Image image(width, height, true, false);
    for(int y=0;y<height;y++) for(int x=0;x<width;x++) image(x,y) = byte4(0xFF,0xFF,0xFF,bitmap.buffer[y*bitmap.pitch+x]);
    glyph.image = move(image);
    return glyph;
}
