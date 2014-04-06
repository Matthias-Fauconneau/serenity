#pragma once
#include "widget.h"
#include "function.h"

/// Displays active notes on a keyboard representation
struct Keyboard : Widget {
    array<uint> midi, input;
    signal<> contentChanged;
    signal<uint,uint> noteEvent;
    void inputNoteEvent(uint key, uint vel);
    void midiNoteEvent(uint key, uint vel);
    int2 sizeHint() { return int2(1056,102); }
    void render(const Image& target);
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button);
};
