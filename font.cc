#include "font.h"

#include "array.cc"
Array_Default(Glyph)
Array_Default(Font::GlyphCache)

static int fonts() { static int fd = openFolder("/usr/share/fonts"_); return fd; }

Font::Font(string name) : keep(mapFile(name,fonts())) {
    DataStream s(keep);
    s.bigEndian=true;
    uint32 unused scaler=s.read();
    uint16 numTables=s.read(), unused searchRange=s.read(), unused numSelector=s.read(), unused rangeShift=s.read();
    for(int i=0;i<numTables;i++) {
        uint32 tag=s.read<uint32>(), unused checksum=s.read(), table=s.read(), unused size=s.read();
        if(tag==raw<uint32>("cmap"_)) cmap=DataStream(array<byte>(s.buffer.data()+table,size));
    }
}

uint16 Font::index(uint16 code) {
    cmap.seek(0); DataStream& s = cmap;
    uint16 unused version=s.read(), numTables=s.read();
    for(int i=0;i<numTables;i++) {
        uint16 unused platformID=s.read(), unused platformSpecificID=s.read(); uint32 subtable=s.read();
        uint index=s.index; s.seek(subtable);
        uint16 format = s.read(), unused size=s.read(), unused language=s.read();
        if(format==4) {
            uint16 segCount=s.read<uint16>()/2, unused searchRange=s.read(),unused entrySelector=s.read(), unused rangeShift=s.read();
            array<uint16> endCode = s.read<uint16>(segCount);
            s.advance(2); //pad
            array<uint16> startCode = s.read<uint16>(segCount);
            array<uint16> idDelta = s.read<uint16>(segCount);
            array<uint16> idRangeOffset = s.read<uint16>(segCount);
            int i=0; while(endCode[i] < code) i++;
            if(startCode[i]<=code) {
                if(idRangeOffset[i]) return *( &idRangeOffset[i] + idRangeOffset[i] / 2 + (code - startCode[i]) );
                else return idDelta[i] + code;
            }
        } else error("Unsupported");
        s.index=index;
    }
    error("Not Found");
}

FontMetrics Font::metrics(int /*size*/) {
    //FontMetrics metrics = { face->size->metrics.descender/64.f, face->size->metrics.ascender/64.f, face->size->metrics.height/64.f };
    FontMetrics metrics = {};
    return metrics;
}

float Font::kerning(int /*leftCode*/, int /*rightCode*/) {
    //return kerning.x/64.f;
    return 0;
}

GlyphMetrics Font::metrics(int /*size*/, int /*code*/) {
    GlyphMetrics metrics={
    //vec2(face->glyph->advance.x / 64.f, face->glyph->advance.y / 64.f),
    //vec2(face->glyph->metrics.width / 64.f, face->glyph->metrics.height / 64.f)
    };
    return metrics;
}

Glyph& Font::glyph(int size, int code) {
    GlyphCache& glyphs = cache[size];
    if(!glyphs.values) glyphs.values.reserve(256); //FIXME: dynamic array would invalid any references
    assert(glyphs.values.capacity()==256);
    Glyph& glyph = glyphs[code]; //TODO: lookup for code in [0x20..0x80]
    if(glyph.pixmap || glyph.advance.x) return glyph;

    glyph.advance = metrics(size,code).advance;
    if(code == ' ') return glyph;
    /*FT_Render_Glyph(face->glyph, FT_RENDER_MODE_LCD);
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
    }*/
    return glyph;
}
