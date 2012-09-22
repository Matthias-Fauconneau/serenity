#pragma once
#include "array.h"
#include "vector.h"
#include "map.h"
#include "function.h"
#include "display.h"

typedef array<int> Chord;
struct Score {
    void onPath(const ref<vec2>&);
    void onGlyph(int, vec2, float,const ref<byte>&, int);

    array<float> staffs;
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

    void synchronize(map<int,Chord>&& chords);
    map<int,Chord> chords; // chronological MIDI notes key values
    array<vec2> positions; // MIDI-synchronized note positions in associated PDF
    array<int> noteIndices; // MIDI-synchronized glyph indices in associated PDF

    uint chordIndex=-1, noteIndex=0;
    array<int> currentChord;
    map<int,int> active;
    map<int,int> expected;
    void seek(uint time);
    void noteEvent(int,int);
    signal<const map<int,byte4>&> highlight;

    /*struct Debug {
      vec2 pos;
      string text;
      Debug(){}
      Debug(vec2 pos,string text):pos(pos),text(text){}
    };
    array<Debug> debug;*/
};
