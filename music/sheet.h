/// \file sheet.h
#pragma once
#include "notation.h"
#include "widget.h"
#include "font.h"

inline String str(const Note& a) { return str(a.key); }

/// Layouts musical notations to graphic primitives
struct Sheet : Widget {
    // Layout parameters
	static constexpr int staffCount = 2;
	static constexpr float halfLineInterval = 3, lineInterval = 2*halfLineInterval;
	const float lineWidth = 1, barWidth=1, stemWidth = 1, stemLength = 7*halfLineInterval, beamWidth = halfLineInterval;
	const float shortStemLength = 7*halfLineInterval;
    // Layout helpers
	float staffY(uint staff, int clefStep) { return (!staff)*(10*lineInterval+halfLineInterval) - clefStep * halfLineInterval; } // Clef independent
	float Y(uint staff, ClefSign clefSign, int step) { return staffY(staff, step-(clefSign==Treble ? 10 : -2)); } // Clef dependent
	// Translates C4 step to top line step using clef
	int clefStep(Sign sign) {
		assert_(sign.type==Sign::Note);
		return sign.note.step - (sign.note.clef.clefSign==Treble ? 10 : -2) - sign.note.clef.octave*7;
	}
	float Y(Sign sign) { assert_(sign.type==Sign::Note); return staffY(sign.staff, clefStep(sign)); } // Clef dependent

    // Fonts
	Font graceFont {File("emmentaler-26.otf", Folder("/usr/local/share/fonts"_)), 4.f*halfLineInterval, "Emmentaler"};
	Font font {File("emmentaler-26.otf", "/usr/local/share/fonts"_), 9.f*halfLineInterval, "Emmentaler"};
	float textSize = 6*halfLineInterval;
    // Font helpers
	vec2 glyphSize(string name) { return font.metrics(font.index(name)).size; }
	float glyphAdvance(string name) { return font.metrics(font.index(name)).advance; }
	float space = 1; //glyphSize("flags.u3"_).x;

	// Graphics
	map<int64, float> measureBars; // Maps sheet time to position of measure starts
	map<Rect, shared<Graphics>> measures;
	array<shared<Graphics>> pages; // FIXME: Page[]:Line[]:Measures[]
	shared<Graphics> debug;

	int lowestStep = 0, highestStep = 0;
	int2 sizeHint(int2) override { return int2(measureBars.values.last(), -(staffY(0, lowestStep)-staffY(1, highestStep))); }
	shared<Graphics> graphics(int2 size, Rect clip) override;

	// -- Control
	array<size_t> measureToChord; // First chord index of measure
	array<size_t> chordToNote; // First note index of chord

	/// Returns measure index containing position \a x
	size_t measureIndex(float x);
	int stop(int unused axis, int currentPosition, int direction) override;

	// -- MIDI Synchronization
	int64 ticksPerMinutes = 0;
	buffer<Sign> midiToSign; /// Sign of corresponding note for each MIDI note
	uint extraErrors = 0, missingErrors = 0, wrongErrors = 0, orderErrors = 0;
	size_t firstSynchronizationFailureChordIndex = -1;

	// -- Page layout
	int2 pageSize = 0;
	array<int> pageBreaks;
	size_t pageIndex = 0;

	/// Layouts musical notations to graphic primitives
	Sheet(ref<Sign> signs, uint ticksPerQuarter, ref<uint> midiNotes={}, int2 pageSize=0, string title="");

	/// Turn pages
	bool keyPress(Key key, Modifiers modifiers) override;
};
