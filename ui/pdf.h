#pragma once
/// \file pdf.h Portable Document Format renderer
#include "string.h"
#include "vector.h"
#include "map.h"
#include "font.h"
#include "widget.h"
#include "function.h"
#include "time.h"
#include "variant.h"
#include "matrix.h"

/*/// 2D affine transformation
struct mat3x2 {
    float data[3*2];
    mat3x2(float d=1) : data{d,0, 0,d, 0,0} {}
    mat3x2(float dx, float dy) : data{1,0, 0,1, dx,dy} {}
    mat3x2(float m00, float m01, float m10, float m11, float dx, float dy):data{m00,m10,m01,m11, dx,dy}{assert(m01==0 && m10==0);}

    float M(int i, int j) const {assert(i<2 && j<3); return data[j*2+i]; }
    float& M(int i, int j) {assert(i<2 && j<3); return data[j*2+i]; }
    float operator()(int i, int j) const { return M(i,j); }
    float& operator()(int i, int j) { return M(i,j); }

    mat3x2 operator*(mat3x2 b) const {mat3x2 r(0); for(int i=0;i<2;i++) { for(int j=0;j<3;j++) for(int k=0;k<2;k++) r.M(i,j)+=M(i,k)*b.M(k,j); r.M(i,2)+=M(i,2); } return r; }
    vec2 operator*(vec2 v) const {vec2 r; for(int i=0;i<2;i++) r[i] = v.x*M(i,0)+v.y*M(i,1)+1*M(i,2); return r; }
};*/

/*/// 2D affine transformation (FIXME: use matrix:mat3x2)
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
};*/

/// Portable Document Format renderer
struct PDF : Widget {
    void clear() { images.clear(); blits.clear(); lines.clear(); fonts.clear(); characters.clear(); polygons.clear(); annotations.clear(); }
    void open(const ref<byte>& data);
    int2 sizeHint() override;
    void render() override { render(0, target.size()); }
    void render(int2 offset, int2 size);

    // Current page rendering context
    mat3x2 Tm,Cm;
    vec2 boxMin, boxMax;
    vec2 pageMin, pageMax;
    void extend(vec2 p) { pageMin=min(pageMin, p), pageMax=max(pageMax, p); }

    // Rendering primitives
    struct Line { vec2 a,b; bool operator <(const Line& o) const{return a.y<o.a.y || b.y<o.b.y;}};
    array<Line> lines;
    enum Flags { Close=1,Stroke=2,Fill=4,OddEven=8,Winding=16,Trace=32 };
    void drawPath(array<array<vec2>>& paths, int flags);

    struct Fonts {
        String name;
        buffer<byte> data;
        map<float, Font> fonts;
        array<float> widths;
    };
    map<string, Fonts> fonts;
    Font& getFont(Fonts& fonts, float size);

    struct Character {
        Fonts* fonts; float size; uint16 index; vec2 position; uint16 code;
        bool operator <(const Character& o) const{return position.y<o.position.y;}
    };
    array<Character> characters;
    void drawText(Fonts& fonts, int fontSize, float spacing, float wordSpacing, const string& data);

    map<String, Image> images;
    struct Blit {
        vec2 position, size; Image image; Image resized;
        bool operator <(const Blit& o) const{return position.y<o.position.y;}
    };
    array<Blit> blits;

    struct Polygon {
        vec2 min,max; array<Line> edges;
    };
    array<Polygon> polygons;

    // Document height (normalized by width=1]
    float height;

    map<int,vec3> colors;
    /// Overrides color for the given characters
    void setColors(const map<int,vec3>& colors) { this->colors=copy(colors); contentChanged(); }
    map<vec2, String> annotations;
    /// Renders additionnal text annotations
    void setAnnotations(const map<vec2, String>& annotations) { this->annotations=copy(annotations); contentChanged(); }

    signal<> contentChanged;
    signal<int> hiddenHighlight;
    int lastSize = 0;
};
