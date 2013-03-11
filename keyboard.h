#pragma once
#include "widget.h"
#include "function.h"

/// Displays active notes on a keyboard representation
struct Keyboard : Widget {
    array<int> midi, input;
    signal<> contentChanged;
    signal<uint,uint> noteEvent;
    void inputNoteEvent(uint key, uint vel);
    void midiNoteEvent(uint key, uint vel);
    int2 sizeHint() { return int2(1056,102); }
    void render(int2 position, int2 size);
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button);
};
