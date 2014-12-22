#pragma once
/// \file pdf.h Portable Document Format renderer
#include "widget.h"
#include "matrix.h"
#include "font.h"
#include "function.h"

/// Portable Document Format renderer
struct PDF : Widget {
    PDF(const ref<byte>& data);
    int2 sizeHint(int2) override;
    shared<Graphics> graphics(int2) override;

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

    array<shared<Graphics>> pages;
    size_t pageIndex = 0;

    // Document height (normalized by width=1]
    float height;
};
