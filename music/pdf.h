#pragma once
/// \file pdf.h Portable Document Format renderer
#include "string.h"
#include "vector.h"
#include "map.h"
#include "font.h"
#include "widget.h"
#include "function.h"
#include "matrix.h"

/// Portable Document Format renderer
struct PDF : Widget {
    void clear() { images.clear(); blits.clear(); lines.clear(); fonts.clear(); characters.clear(); paths.clear(); polygons.clear(); annotations.clear(); }
    void open(const ref<byte>& data);
    int2 sizeHint() override;
    void render(int2 position, int2 size) override;

    mat3x2 Tm,Cm;
    float x1,y1,x2,y2;
    void extend(vec2 p) { if(p.x<x1) x1=p.x; if(p.x>x2) x2=p.x; if(p.y<y1) y1=p.y; if(p.y>y2) y2=p.y; }

    struct Line { vec2 a,b; bool operator <(const Line& o) const{return a.y<o.a.y || b.y<o.b.y;}};
    array<Line> lines;
    enum Flags { Close=1,Stroke=2,Fill=4,OddEven=8,Winding=16,Trace=32 };
    void drawPath(array<array<vec2>>& paths, int flags);

    struct Font {
        String name;
        unique< ::Font> font;
        array<float> widths;
    };
    map<string, Font> fonts;
    struct Character { Font* font; float size; uint16 index; vec2 pos; uint16 code; bool operator <(const Character& o) const{return pos.y<o.pos.y;}};
    array<Character> characters;
    void drawText(Font* font, int fontSize, float spacing, float wordSpacing, const string& data);

    map<String, Image> images;
    struct Blit { vec2 pos,size; Image image; Image resized; bool operator <(const Blit& o) const{return pos.y<o.pos.y;}};
    array<Blit> blits;

    struct Polygon { vec2 min,max; array<Line> edges; };
    array<Polygon> polygons;

    /// Hooks which can be used to provide additionnal semantics or interactions to the PDF document
    signal<int /*index*/, vec2 /*position*/, float /*size*/,const string& /*font*/, int /*code*/, int /*fontIndex*/> onGlyph;
    signal<const ref<vec2>&> onPath;
    array<array<vec2>> paths;
    float normalizedScale = 0; // normalize positions (scale PDF width to 1280)

    map<int,vec4> colors;
    /// Overrides color for the given characters
    void setColors(const map<int,vec4>& colors) { this->colors=copy(colors); contentChanged(); }
    map<vec2, String> annotations;
    /// Renders additionnal text annotations
    void setAnnotations(const map<vec2, String>& annotations) { this->annotations=copy(annotations); contentChanged(); }

    signal<> contentChanged;
    signal<int> hiddenHighlight;
    float scale=2;
};
