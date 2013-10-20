#pragma once
/// \file pdf.h Portable Document Format renderer
#include "string.h"
#include "vector.h"
#include "map.h"
#include "font.h"
#include "widget.h"
#include "function.h"
//include "matrix.h" FIXME
#include "time.h"
#include "gl.h"

/// 2D affine transformation (FIXME: use matrix:mat3x2)
struct mat3x2 {
    float m11, m12, m21, m22, dx, dy;
    mat3x2(float m11, float m12, float m21, float m22, float dx, float dy):m11(m11),m12(m12),m21(m21),m22(m22),dx(dx),dy(dy){}
    mat3x2(float dx, float dy) : mat3x2(1,0,0,1,dx,dy) {}
    mat3x2() : mat3x2(1,0,0,1,0,0) {}
    float M(int i, int j) const {assert(i<2 && j<3); return (&m11)[j*2+i]; }
    float& M(int i, int j) {assert(i<2 && j<3); return (&m11)[j*2+i]; }
    float operator()(int i, int j) const { return M(i,j); }
    float& operator()(int i, int j) { return M(i,j); }
    mat3x2 operator*(mat3x2 m) const { return mat3x2( m11*m.m11 + m12*m.m21, m11*m.m12 + m12*m.m22,
                                                          m21*m.m11 + m22*m.m21, m21*m.m12 + m22*m.m22,
                                                          dx*m.m11  + dy*m.m21 + m.dx, dx*m.m12  + dy*m.m22 + m.dy ); }
    vec2 operator*(vec2 v) const { return vec2( m11*v.x + m21*v.y + dx, m12*v.x + m22*v.y + dy ); }
};

/// Portable Document Format renderer
struct PDF : Widget {
    void clear() {
        images.clear(); blits.clear(); lines.clear(); fonts.clear(); characters.clear(); paths.clear(); polygons.clear();
        annotations.clear();
    }
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
        map<uint16, GLTexture> cache;
    };
    map<string, Font> fonts;
    struct Character {
        Font* font; float size; uint16 index; vec2 position; uint16 code;
        bool operator <(const Character& o) const{return position.y<o.position.y;}
    };
    array<Character> characters;
    void drawText(Font* font, int fontSize, float spacing, float wordSpacing, const string& data);

    map<String, Image> images;
    struct Blit {
        vec2 position, size; Image image; Image resized;
        bool operator <(const Blit& o) const{return position.y<o.position.y;}
    };
    array<Blit> blits;

    struct Polygon { vec2 min,max; array<Line> edges; };
    array<Polygon> polygons;

    struct GLBlit { vec2 min,max; uint texture; };
    array<GLBlit> glblits;

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
