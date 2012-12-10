#pragma once
/// \file score.h Sheet music notation recognition from PDF files (with MIDI synchronization)
#include "array.h"
#include "vector.h"
#include "map.h"
#include "function.h"
#include "display.h"
#include "midi.h"

struct Score {
    void onPath(const ref<vec2>&);
    void onGlyph(int, vec2, float,const ref<byte>&, int, int);

    array<float> staffs;
    vec2 lastClef=0, lastPos=0;
    uint staffCount=0;
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
        Note(int index, int duration) : index(index), duration(duration) {}
        int index,duration,scoreIndex=-1;
    };
    typedef map<int, map< int, Note> > Staff;
    array<Staff> notes; //[staff][x][y]
    map<int, array<vec2> > dots;
    map<vec2,Note> nearStaffLimit;

    void parse();
    void synchronize(const map<uint,Chord>& MIDI);
    void annotate(map<uint,Chord>&& chords);
    signal<const map<uint,Chord>&> annotationsChanged;
    map<uint,Chord> chords; // chronological MIDI notes key values
    array<vec2> positions; // MIDI-synchronized note positions in associated PDF
    array<int> indices; // MIDI-synchronized character indices in associated PDF

    uint chordIndex=-1, noteIndex=0, currentStaff=0;
    int chordSize=0;
    map<int,int> active;
    map<int,int> miss;
    map<int,int> expected;
    bool editMode=false;
    bool showActive=false; // Toggles between active or expected notes highlighting
    void toggleEdit();
    void previous();
    void next();
    void insert();
    void remove();
    void seek(uint time);
    void noteEvent(int,int);
    signal<const map<int,vec4>&> activeNotesChanged;
    signal<float,float,float> nextStaff;
    map<vec2, string> debug;
    int pass=-1;
    int msScore=0;

    void clear() { staffs.clear(); lastClef=lastPos=0; repeats.clear(); ties.clear(); tails.clear(); tremolos.clear(); trills.clear(); notes.clear(); dots.clear(); chords.clear(); positions.clear(); indices.clear(); chordIndex=-1, noteIndex=0, currentStaff=0; active.clear(); miss.clear(); expected.clear(); debug.clear(); pass=-1; nearStaffLimit.clear(); }
};
