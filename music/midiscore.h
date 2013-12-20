#pragma once
#include "widget.h"
#include "font.h"
#include "midi.h"

struct MidiScore : Widget {
    enum Clef { Bass, Treble };
    int keys[4][11] = {
        {1,0,1,0,1,1,0,1,0,1,0}, // F minor
        {0,1,0,1,1,0,1,0,1,0,1},  // C major (should be same for all majors)
        {0,1,0,1,1,0,1,0,1,0,1},  // G major TESTME
        {0,1,0,1,1,0,1,0,1,0,1}  // D major TESTME
    };
    int accidentals[4][12] = { // 0=nothing, 1=♭, 2=♯, 3=♮ (TODO)
        {0,1,0,1,0,0,1,0,1,0,0,3}, // F minor
        {0,2,0,2,0,0,2,0,2,0,2,0},  // C major
        {0,2,0,2,0,3,0,0,2,0,2,0},  // G major TODO
        {3,0,0,2,0,3,0,0,2,0,2,0}  // D major TODO
    };
    map<uint, MidiChord> notes;
    int key=-1; uint tempo=120; uint timeSignature[2] = {4,4};

    const int staffCount = 2;
    const int measuresPerStaff = 3;
    const int staffInterval = 12, staffMargin = 4*staffInterval, staffHeight = staffMargin+4*staffInterval+staffMargin, systemHeight=staffCount*staffHeight+staffMargin;
    const int systemHeader = 128;
    int ticksPerBeat;
    int beatsPerMeasure;
    int staffTime;

    array<float> staffs;
    array<vec2> positions;

    void parse(map<uint,Chord>&& notes, int unused key, uint tempo, uint timeSignature[2], uint ticksPerBeat);
    int2 sizeHint();

    // Returns staff coordinates from note  (for a given clef and key)
    int staffY(Clef clef, int note);

    int2 position=0, size=0;
    // Returns staff X position from time
    int staffX(int t);

    // Returns page coordinates from staff coordinates
    int2 page(int staff, int t, int h);

    // Draws a staff
    void drawStaff(int t, int staff, Clef clef);
    // Draws a ledger line
    void drawLedger(int staff, int t, int h);

    void render(int2 position, int2 size);
    map<int,vec4> colors;
    signal<> contentChanged;
    void setColors(const map<int,vec4>& colors) { this->colors=copy(colors); contentChanged(); }
};
