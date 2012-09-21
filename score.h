#pragma once
#include "array.h"
#include "vector.h"
#include "map.h"

struct Score {
    array<float> staffs;
    array<vec2> positions;
    //array<int> noteIndices;

    void onGlyph(int, vec2, float,const ref<byte>&, int);
    void onPath(const ref<vec2>&);
    void synchronize(array<int> MIDI);

    vec2 lastClef;
    array<vec2> repeats;

    struct Line {
        vec2 a,b;
        Line(vec2 a, vec2 b):a(a),b(b){}
        bool operator ==(Line o) const { return a==o.a && b==o.b; }
    };
    array<Line> ties;
    array<Line> tails;
    array<Line> tremolos;
    array<Line> trills;

    struct Note {
        Note() : index(1), duration(1) {}
        Note(int index, int duration) : index(index), duration(duration) {}
        int index,duration;
    };
    map<int, map<int, map< int, Note> > > notes; //[staff][x][y]
    map<int, array<vec2> > dots;

    /*struct Debug {
      vec2 pos;
      string text;
      Debug(){}
      Debug(vec2 pos,string text):pos(pos),text(text){}
    };
    array<Debug> debug;*/
};
