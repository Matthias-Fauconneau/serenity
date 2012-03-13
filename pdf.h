#pragma once
#include "string.h"
#include "vector.h"
#include "map.h"
#include "font.h"
#include "interface.h"

struct PDF : Widget {
    enum Flags { Close=1,Stroke=2,Fill=4,OddEven=8,Winding=16,Trace=32 };

    float x1,y1,x2,y2;
    void extend(vec2 p) { if(p.x<x1) x1=p.x; if(p.x>x2) x2=p.x; if(p.y<y1) y1=p.y; if(p.y>y2) y2=p.y; }

    map<string, Font> fonts;
    mat32 Tm,Cm;
    float recognitionScale; //hack to avoid changing scale of recognition values
    array< array<vec2> > paths; //only for recognition
    array< vec2 > lines;
    array< uint > indices; array< vec2 > vertices; //filled curves
    struct Character { vec2 position; Font* font; float size; int code; };
    array<Character> characters;

    float scroll=0;
    GLBuffer stroke;
    GLBuffer fill;
    struct Blit { vec2 min,max; uint id; };
    array<Blit> blits;

    signal<int, vec2, float,const string&, int> onGlyph;
    signal<const array<vec2>&> onPath;
    signal<> needUpdate;

    void drawPath(array<array<vec2> >& paths, int flags);
    void drawText(Font* font, int fontSize, float spacing, float wordSpacing, const string& data);

    void open(const string& path);
    void render(int2 parent);
    bool mouseEvent(int2, Event, Button) override;

    operator bool() { return characters.size(); }
};
