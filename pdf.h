#pragma once
/// \file pdf.h Portable Document Format renderer
#include "string.h"
#include "vector.h"
#include "map.h"
#include "font.h"
#include "widget.h"
#include "function.h"

/// 2D affine transformation
struct mat32 {
    float m11, m12, m21, m22, dx, dy;
    mat32(float m11, float m12, float m21, float m22, float dx, float dy):m11(m11),m12(m12),m21(m21),m22(m22),dx(dx),dy(dy){}
    mat32(float dx, float dy) : mat32(1,0,0,1,dx,dy) {}
    mat32(vec2 t) : mat32(1,0,0,1,t.x,t.y) {}
    mat32() : mat32(1,0,0,1,0,0) {}
    mat32 operator*(mat32 m) const { return mat32( m11*m.m11 + m12*m.m21, m11*m.m12 + m12*m.m22,
                                                          m21*m.m11 + m22*m.m21, m21*m.m12 + m22*m.m22,
                                                          dx*m.m11  + dy*m.m21 + m.dx, dx*m.m12  + dy*m.m22 + m.dy ); }
    vec2 operator*(vec2 v) const { return vec2( m11*v.x + m21*v.y + dx, m12*v.x + m22*v.y + dy ); }
};

/// Portable Document Format renderer
struct PDF : Widget {
    Map file;
    void open(const ref<byte>& path, const Folder& folder);
    explicit operator bool() { return (bool)file; }
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
