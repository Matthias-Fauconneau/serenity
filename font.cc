#include "font.h"
#define strlen _strlen
#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftlcdfil.h>
#undef strlen

static FT_Library ft;

Font::Font(const string& path) {
	FT_Init_FreeType(&ft);
	FT_Library_SetLcdFilter(ft,FT_LCD_FILTER_DEFAULT);
	FT_New_Face(ft, strz(path).data, 0, &face);
	assert(face,path);
}

FontMetrics Font::metrics(int size) {
	FT_Set_Char_Size(face, 0, size, 72, 72);
	FontMetrics metrics = { face->size->metrics.descender>>6, face->size->metrics.ascender>>6, face->size->metrics.height>>6 };
	return metrics;
}

int Font::kerning(char leftCode, char rightCode) {
	int left = FT_Get_Char_Index(face, leftCode); assert(left,"glyph not found '",leftCode,"'\n");
	int right = FT_Get_Char_Index(face, rightCode); assert(right,"glyph not found '",rightCode,"'\n");
	FT_Vector kerning;
	FT_Get_Kerning(face, left, right, FT_KERNING_DEFAULT, &kerning );
	return kerning.x>>6;
}

GlyphMetrics Font::metrics(int size, char code) {
	assert(face);
	FT_Set_Char_Size(face, 0, size, 72, 72);
	int index = FT_Get_Char_Index(face, code);
	if(!index) index=code;
	FT_Load_Glyph(face, index, FT_LOAD_TARGET_LCD);
	GlyphMetrics metrics={
	vec2(face->glyph->advance.x / 64.0, face->glyph->advance.y / 64.0),
	vec2(face->glyph->metrics.width / 64.0, face->glyph->metrics.height / 64.0)
	};
	return metrics;
}

Glyph& Font::glyph(int size, char code) {
	map<int, Glyph>& glyphs = cache[size];
	if(glyphs.values.size==0) glyphs.values.reserve(256); //realloc would invalid any references
	assert(glyphs.values.capacity==256);
	Glyph& glyph = glyphs[(int)code];
	if(glyph.texture || glyph.advance.x) return glyph;

	glyph.advance = metrics(size,code).advance;
	if(code == ' ') return glyph;
	FT_Render_Glyph(face->glyph, FT_RENDER_MODE_LCD);
	glyph.offset = vec2( face->glyph->bitmap_left, -face->glyph->bitmap_top );
	FT_Bitmap bitmap=face->glyph->bitmap;
	//assert(bitmap.buffer);
	if(!bitmap.buffer) return glyph;
	int width = bitmap.width/3, height = bitmap.rows;
	byte4* data = new byte4[height*width];
	for(int y=0;y<height;y++) for(int x=0;x<width;x++) {
		uint8* rgb = &bitmap.buffer[y*bitmap.pitch+x*3];
		data[y*width+x] = byte4(255-rgb[0],255-rgb[1],255-rgb[2],255);
	}
	glyph.texture = GLTexture(Image((uint8*)data,width,height,4));
	return glyph;
}

Font defaultFont(_("/usr/share/fonts/dejavu/DejaVuSans.ttf"));
