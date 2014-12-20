/// \file sheet.h
#pragma once
#include "notation.h"
#include "widget.h"
#include "font.h"

/// Layouts musical notations to graphic primitives
struct Sheet : Widget {
    // Layout parameters
	static constexpr int staffCount = 2;
	const float halfLineInterval, lineInterval = 2*halfLineInterval;
	const float stemWidth = 0, stemLength = 7*halfLineInterval, beamWidth = halfLineInterval;
	const float shortStemLength = 5*halfLineInterval;
    // Layout helpers
	float staffY(uint staff, int clefStep) { return (!staff)*(10*lineInterval+halfLineInterval) - clefStep * halfLineInterval; }
	int clefStep(Clef clef, int step) { return step - (clef.clefSign==GClef ? 10 : -2) - clef.octave*7; } // Translates C4 step to top line step using clef
	int clefStep(Sign sign) { assert_(sign.type==Sign::Note); return clefStep(sign.note.clef, sign.note.step); }
	float Y(uint staff, Clef clef, int step) { return staffY(staff, clefStep(clef, step)); }
	float Y(Sign sign) { assert_(sign.type==Sign::Note); return staffY(sign.staff, clefStep(sign)); }

    // Fonts
	Font smallFont {File("Bravura.otf", Folder("/usr/local/share/fonts"_)), 6.f*halfLineInterval, "Bravura"};
	Font font {File("Bravura.otf", "/usr/local/share/fonts"_), 8.f*halfLineInterval, "Bravura"};
	string textFont = "LinLibertine";
	float textSize = 6*halfLineInterval;
    // Font helpers
	vec2 glyphSize(uint code, Font* font_=0/*font*/) { Font& font=font_?*font_:this->font; return font.metrics(font.index(code)).size; }
	float glyphAdvance(uint code, Font* font_=0/*font*/) { Font& font=font_?*font_:this->font; return font.metrics(font.index(code)).advance; }
	float space = glyphAdvance(SMuFL::NoteHead::Black);
	float margin = 1;

	// Graphics
	map<int64, float> measureBars; // Maps sheet time to position of measure starts
	//map<Rect, shared<Graphics>> measures;
	array<Graphics> pages; // FIXME: Page[]:Line[]:Measures[]
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
	int2 pageSize;
	size_t pageIndex = 0;

	/// Layouts musical notations to graphic primitives
	Sheet(ref<Sign> signs, uint ticksPerQuarter, ref<uint> midiNotes={}, float halfLineInterval = 4, int2 pageSize=0, string title="");

	/// Turn pages
	bool keyPress(Key key, Modifiers modifiers) override;
};
