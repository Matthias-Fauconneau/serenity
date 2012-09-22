#pragma once
#include "string.h"
#include "vector.h"
#include "matrix.h"
#include "map.h"
#include "font.h"
#include "widget.h"
#include "function.h"

struct PDF : Widget {
    Map file;
    void open(const ref<byte>& path, const Folder& folder);
    int2 sizeHint() override;
    void render(int2 position, int2 size) override;

    mat32 Tm,Cm;
    float x1,y1,x2,y2;
    void extend(vec2 p) { if(p.x<x1) x1=p.x; if(p.x>x2) x2=p.x; if(p.y<y1) y1=p.y; if(p.y>y2) y2=p.y; }

    struct Line { vec2 a,b; };
    array<Line> lines;
    enum Flags { Close=1,Stroke=2,Fill=4,OddEven=8,Winding=16,Trace=32 };
    void drawPath(array<array<vec2> >& paths, int flags);

    struct Font {
        string name;
        array<float> widths;
        ::Font font;
    };
    map<ref<byte>, Font*> fonts;
    struct Character { Font* font; float size; uint16 index; vec2 pos; uint16 code; };
    array<Character> characters;
    void drawText(Font* font, int fontSize, float spacing, float wordSpacing, const ref<byte>& data);

    /// Hooks which can be used to provide additionnal semantics or interactions to the PDF document
    signal<int /*index*/, vec2 /*position*/, float /*size*/,const ref<byte>& /*font*/, int /*code*/> onGlyph;
    signal<const ref<vec2>&> onPath;
    array< array<vec2> > paths;
    float normalizedScale; // scale semantic positions to "pixels" (normalize width to 1280)

    map<int,byte4> colors;
    /// Overrides color for the given characters
    void setColors(const map<int,byte4>& colors) { this->colors=copy(colors); contentChanged(); }
    map<vec2, string> annotations;
    /// Renders additionnal text annotations
    void setAnnotations(const map<vec2, string>& annotations) { this->annotations=copy(annotations); contentChanged(); }

    signal<> contentChanged;
    signal<int> hiddenHighlight;
};
