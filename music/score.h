#pragma once
/// \file score.h Sheet music notation recognition from PDF files (with MIDI synchronization)
#include "array.h"
#include "vector.h"
#include "map.h"
#include "function.h"
#include "graphics.h"
#include "midi.h"

struct Chord : array<uint> { uint firstNoteIndex; Chord(uint index):firstNoteIndex(index){} };

struct Score {
    void onPath(const ref<vec2>&);
    void onGlyph(int, vec2, float,const string&, int, int);

    array<float> staffs; array<float> keys;
    vec2 previousClef=0, lastClef=0, lastPos=0; //float maxStaffDistance=100_px;
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
    array<vec2> ledgers;
    array<float> staffLines;

    struct Note {
        Note(uint index, uint duration) : pdfIndex(index), duration(duration) {}
        uint pdfIndex; // into PDF::characters
        uint duration; // Note duration
        uint scoreIndex=-1; // into positions/indices/durations
    };
    typedef map<float, Note> Chord;
    typedef map<float, Chord> Staff;
    array<Staff> notes; //[staff][x][y]
    map<int, array<vec2>> dots;
    //map<vec2,Note> nearStaffLimit;

    void parse();
    /// Synchronize notes to MIDI track
    void synchronize(const ref<MidiNote>& notes);

    array< ::Chord> chords; // Maps ordered MIDI events to note indices
    array<vec2> positions; // MIDI-synchronized note positions in associated PDF
    array<int> indices; // MIDI-synchronized character indices in associated PDF
    array<int> durations; // MIDI-synchronized character indices in associated PDF (for manual annotations)

    uint chordIndex=-1, currentStaff=0; float currentX=0;
    int chordSize=0; //Initial expected set size (to allow missing notes on large chords)
    map<uint,uint> active; // Maps active keys to notes (indices)
    map<uint,uint> expected; // Maps expected keys to notes (indices)
    bool showActive=false; // Toggles whether active notes are highlighted
    bool showExpected=false; // Toggles whether expected notes are highlighted (set on errors, cleared on sucess)
    int errors=0;
#if ANNOTATION
    bool editMode=false;
    void toggleEdit();
    void previous();
    void next();
    void insert();
    void remove();
    void annotate(map<uint,MidiChord>&& chords);
    signal<const map<uint,MidiChord>&> annotationsChanged;
#endif
    void expect();
    void seek(uint time);
    void noteEvent(uint, uint);
    signal<const map<int,vec3>&> activeNotesChanged;
    signal<float,float,float> nextStaff;
    map<vec2, String> debug;
    int pass=-1;
    int msScore=0; //HACK
    map<int,int> histogram;
    const int quarter=9, half=17, whole=16;

    void clear() {
        staffs.clear(); keys.clear(); previousClef=lastClef=lastPos=0; /*maxStaffDistance=100;*/ staffCount=0;
        repeats.clear(); ties.clear(); tails.clear(); ledgers.clear(); staffLines.clear(); tremolos.clear(); trills.clear();
        notes.clear(); dots.clear(); chords.clear(); positions.clear(); indices.clear(); durations.clear(); chordIndex=-1, currentStaff=0;
        active.clear(); expected.clear(); debug.clear(); pass=-1; histogram.clear(); errors=0;
    }
};