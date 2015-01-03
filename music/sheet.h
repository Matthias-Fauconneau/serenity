/// \file sheet.h
#pragma once
#include "notation.h"
#include "widget.h"
#include "font.h"

/// Layouts musical notations to graphic primitives
struct Sheet : Widget {
	// Graphics
	map<uint, float> measureBars; // Maps sheet time to position of measure starts
	array<Graphics> pages; // FIXME: Page[]:Line[]:Measures[]
	shared<Graphics> debug;

	int lowestStep = 0, highestStep = 0;
    vec2 sizeHint(vec2) override;
    shared<Graphics> graphics(vec2 size, Rect clip) override;

	// -- Control
	array<size_t> measureToChord; // First chord index of measure
	array<size_t> chordToNote; // First note index of chord

	/// Returns measure index containing position \a x
	size_t measureIndex(float x);
    //float stop(vec2 unused size, int unused axis, float currentPosition, int direction) override;

	// -- MIDI Synchronization
	buffer<Sign> midiToSign; /// Sign of corresponding note for each MIDI note
	uint extraErrors = 0, missingErrors = 0, wrongErrors = 0, orderErrors = 0;
	size_t firstSynchronizationFailureChordIndex = -1;

	// -- Page layout
	int2 pageSize;
	size_t pageIndex = 0;

	/// Layouts musical notations to graphic primitives
    Sheet(ref<Sign> signs, uint ticksPerQuarter, int2 pageSize=0, float halfLineInterval = 4, ref<uint> midiNotes={}, string title="", bool pageNumbers=false);

	/// Turn pages
	bool keyPress(Key key, Modifiers modifiers) override;
};
