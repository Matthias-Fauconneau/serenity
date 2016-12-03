#pragma once
#include "widget.h"

/// Displays active notes on a keyboard representation
struct Keyboard : Widget {
    array<uint> left, right;
    void midiNoteEvent(uint key, uint vel);
    vec2 sizeHint(vec2) override { return vec2(1920, 210); }
    shared<Graphics> graphics(vec2 size) override;
};
