#include <ft2build.h>
#include <freetype2/freetype.h> //freetype
#include <freetype2/ftlcdfil.h>

#include "font.h"
#include "file.h"

const Folder& fontFolder() { static Folder folder("/usr/share/fonts"_); return folder; }
String findFont(string fontName, ref<string> fontTypes) {
    array<String> fonts = filter(fontFolder().list(Files|Recursive), [&](string path) {
        if(!endsWith(path,".ttf"_)) return true;
        for(string fontType: fontTypes) {
            if(fontType) {
                if(find(path, fontName+fontType+"."_) ||
                   find(path, fontName+"-"_+fontType+"."_) ||
                   find(path, fontName+"_"_+fontType+"."_) ||
                   find(path, fontName+" "_+fontType+"."_))
                    return false;
            } else if(find(path,fontName+"."_)) {
                return false;
            }
        }
        return true;
    });
    assert_(fonts.size==1, fontName, fontTypes, fonts);
    return move(fonts[0]);
}

static FT_Library ft; static int fontCount=0;
Font::Font(Map&& map, float size, string name, bool hint) : Font(unsafeReference<byte>(map), size, name, hint) { keep=move(map); }
Font::Font(buffer<byte>&& data_, float size, string name, bool hint) : data(move(data_)), size(size), name(name), hint(hint) {
    if(!ft) FT_Init_FreeType(&ft);
    int e; if((e=FT_New_Memory_Face(ft,(const FT_Byte*)data.data,data.size,0,&face)) || !face) { error("Invalid font", data.data, data.size); return; }
    fontCount++;
    assert_(size);
    FT_Size_RequestRec req = {FT_SIZE_REQUEST_TYPE_NOMINAL,long(round(size*0x1p6)),long(round(size*0x1p6)),0,0};
    FT_Request_Size(face,&req);
    ascender=face->size->metrics.ascender*0x1p-6;
    descender=face->size->metrics.descender*0x1p-6;
    vec2 scale (face->size->metrics.x_scale*0x1p-16*0x1p-6, face->size->metrics.y_scale*0x1p-16*0x1p-6);
    bboxMin = scale*vec2(face->bbox.xMin, face->bbox.yMin);
    bboxMax = scale*vec2(face->bbox.xMax, face->bbox.yMax);
}

Font::~Font(){
    if(face) {
        FT_Done_Face(face); face=0; fontCount--;
        assert(fontCount>=0); if(fontCount == 0) { assert(ft); FT_Done_FreeType(ft), ft=0; }
    }
}

uint Font::index(uint code) const {
    for(int i=0;i<face->num_charmaps;i++) {
        FT_Set_Charmap(face, face->charmaps[i] );
        uint index = FT_Get_Char_Index(face, code);
        if(index) return index;
    }
    error("Missing code", code);
    return code;
}

float Font::kerning(uint leftIndex, uint rightIndex) {
    FT_Vector kerning; FT_Get_Kerning(face, leftIndex, rightIndex, FT_KERNING_DEFAULT, &kerning); return kerning.x*0x1p-6;
}

Font::Metrics Font::metrics(uint index) const {
    FT_Load_Glyph(face, index, hint?FT_LOAD_TARGET_NORMAL:FT_LOAD_TARGET_LIGHT);
    return {
        face->glyph->metrics.horiAdvance*0x1p-6f,
        vec2(face->glyph->metrics.horiBearingX*0x1p-6f, face->glyph->metrics.horiBearingY*0x1p-6f),
        (int)face->glyph->lsb_delta, (int)face->glyph->rsb_delta,
        {{face->glyph->metrics.width*0x1p-6f, face->glyph->metrics.height*0x1p-6f}}};
}

Font::Glyph Font::render(uint index) {
    {const Glyph* glyph = cache.find(index);
        if(glyph) return {glyph->offset, share(glyph->image)};}
    Glyph& glyph = cache.insert(index);
    FT_Load_Glyph(face, index, hint?FT_LOAD_TARGET_NORMAL:FT_LOAD_TARGET_LIGHT);
    FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
    glyph.offset = int2(face->glyph->bitmap_left, -face->glyph->bitmap_top);
    FT_Bitmap bitmap=face->glyph->bitmap;
    if(!bitmap.buffer) { return {glyph.offset, share(glyph.image)}; }
    int width = bitmap.width, height = bitmap.rows;
    Image image(width, height, true, false);
    for(int y=0;y<height;y++) for(int x=0;x<width;x++) image(x,y) = byte4(0xFF,0xFF,0xFF,bitmap.buffer[y*bitmap.pitch+x]);
    glyph.image = move(image);
    return {glyph.offset, share(glyph.image)};
}
