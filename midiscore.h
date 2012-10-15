#pragma once
#include "widget.h"
#include "font.h"
#include "midi.h"

struct MidiScore : Widget {
    enum Clef { Bass, Treble };
    int keys[2][11] = {
        {1,0,1,0,1,1,0,1,0,1,0}, // F minor
        {0,1,0,1,1,0,1,0,1,0,1}  // C major
    };
    int accidentals[2][12] = { //1=b, 2=#
        {0,1,0,1,0,0,1,0,1,0,0,0}, // F minor
        {0,2,0,2,0,0,2,0,2,0,2,0}  // C major
    };
    map<int,Chord> notes;
    int key=-1; uint tempo=120; uint timeSignature[2] = {4,4};

    const int staffCount = 2;
    const int staffInterval = 12, staffMargin = 4*staffInterval, staffHeight = staffMargin+4*staffInterval+staffMargin, systemHeight=staffCount*staffHeight+staffMargin;
    const int systemHeader = 128;
    int beatsPerMeasure;
    int staffTime;

    array<float> staffs;
    array<vec2> positions;

    void parse(map<int,Chord>&& notes, int unused key, uint tempo, uint timeSignature[2]);
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
    map<int,byte4> colors;
    signal<> contentChanged;
    void setColors(const map<int,byte4>& colors) { this->colors=copy(colors); contentChanged(); }
};
