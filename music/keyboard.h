#pragma once
#include "widget.h"

/// Displays active notes on a keyboard representation
struct Keyboard : Widget {
	array<uint> left, right;
    void midiNoteEvent(uint key, uint vel);
	int2 sizeHint(int2) override { return int2(1280, 128); }
	shared<Graphics> graphics(int2 size) override;
};
