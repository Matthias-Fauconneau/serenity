/// \file sheet.h
#pragma once
#include "notation.h"
#include "widget.h"
#include "font.h"
#include "midi.h" // FIXME

struct GraphicsWidget : Graphics, Widget {
    GraphicsWidget() {}
    GraphicsWidget(Graphics&& o) : Graphics(move(o)) {}
    vec2 sizeHint(vec2) override { assert_(isNumber(bounds.max), bounds); return bounds.max; }
    shared<Graphics> graphics(vec2) override { return shared<Graphics>((Graphics*)this); }
};

/// Layouts musical notations to graphic primitives
struct Sheet {
    // Graphics
    map<uint, float> measureBars; // Maps sheet time to position of measure starts
    array<Graphics> pages; // FIXME: Page[]:Line[]:Measures[]
    shared<Graphics> debug;

    int lowestStep = 0, highestStep = 0;

    // -- Control
    array<size_t> measureToChord; // First chord index of measure
    array<size_t> chordToNote; // First MIDI note index for each chord

    //float stop(vec2 unused size, int unused axis, float currentPosition, int direction) override;

    // -- MIDI Synchronization
    buffer<Sign> midiToSign; /// Sign of corresponding note for each MIDI note
    uint extraErrors = 0, missingErrors = 0, wrongErrors = 0, orderErrors = 0;
    size_t firstSynchronizationFailureChordIndex = -1;

    // -- Page layout
    int2 pageSize;

    /// Layouts musical notations to graphic primitives
    Sheet(ref<Sign> signs, uint ticksPerQuarter, int2 pageSize=0, float halfLineInterval = 4, ref</*MidiNote*/uint> midiNotes={}, string title="", bool pageNumbers=false);
};
