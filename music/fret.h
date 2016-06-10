#pragma once
#include "widget.h"
#include "notation.h"

/// Displays active notes on a fretboard representation
struct Fret : Widget {
    map<uint, Sign> active, measure;
    vec2 sizeHint(vec2) override { return vec2(1280, 120); }
    shared<Graphics> graphics(vec2 size) override;
};
