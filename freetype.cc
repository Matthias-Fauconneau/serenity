#include "font.h"
#define strlen _strlen
#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftlcdfil.h>
#undef strlen

static FT_Library ft;

struct FreeType : Font {
	FT_FaceRec_* face;
	Glyph glyphs[256];

	FreeType(const string& path, int size) {
		FT_Init_FreeType(&ft);
		FT_Library_SetLcdFilter(ft,FT_LCD_FILTER_DEFAULT);
		FT_New_Face(ft, strz(path).data, 0, &face);
		FT_Set_Char_Size(face, 0, size<<6, 72, 72);
		assert(face,path);
	}
	int height() { return face->size->metrics.height>>6; }
	int ascender() { return face->size->metrics.ascender>>6; }
	Glyph& glyph(char code) {
		Glyph& glyph = glyphs[(int)code];
		if(glyph) return glyph;

		int index = FT_Get_Char_Index(face, code); assert(index,"glyph not found '",code,"'\n");
		FT_Load_Glyph(face, index, FT_LOAD_TARGET_LCD);
		glyph.advance = int2( face->glyph->advance.x >> 6, face->glyph->advance.y >> 6 );
		if(code == ' ') return glyph;
		FT_Render_Glyph(face->glyph, FT_RENDER_MODE_LCD);
		glyph.offset = int2( face->glyph->bitmap_left, -face->glyph->bitmap_top );
		FT_Bitmap bitmap=face->glyph->bitmap;
		assert(bitmap.buffer);
		int width = bitmap.width/3, height = bitmap.rows;
		byte4* data = new byte4[height*width];
		for(int y=0;y<height;y++) for(int x=0;x<width;x++) {
			uint8* rgb = &bitmap.buffer[y*bitmap.pitch+x*3];
			data[y*width+x] = byte4(255-rgb[0],255-rgb[1],255-rgb[2],clip(0,rgb[0]+rgb[1]+rgb[2],255));
		}
		glyph.texture = GLTexture(Image((uint8*)data,width,height,4));
		return glyph;
	}
};

Font* Font::instance(int size) {
	static map<int,Font*> fonts;
	Font*& font = fonts[size];
	if(!font) font = new FreeType(_("/usr/share/fonts/dejavu/DejaVuSansMono.ttf"),size);
	return font;
}
