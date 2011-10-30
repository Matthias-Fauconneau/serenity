#include "gl.h"

struct Font {
	struct Glyph {
		int2 offset;
		int2 advance;
		GLTexture texture;
		inline operator bool() { return texture; }
		inline int2 size() { return { texture.width, texture.height }; }
	};

	virtual int height()=0;
	virtual int ascender()=0;
	virtual Glyph& glyph(char code)=0;
	static Font* instance(int size);
};
