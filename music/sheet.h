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
	const float halfLineInterval = 5, lineInterval = 2*halfLineInterval;
	const float lineWidth = 1, barWidth=1, stemWidth = 1, stemLength = 4*lineInterval, beamWidth = 6;
	const float shortStemLength = 5*halfLineInterval;
    // Layout helpers
    int clefStep(ClefSign clefSign, int step) { return step - (clefSign==Treble ? 10 : -2); } // Translates C4 step to top line step using clef
	float staffY(uint staff, int clefStep) { return staff*10*lineInterval - clefStep * halfLineInterval; } // Clef independent
	int Y(uint staff, Clef clef, int step) { return staffY(staff, clefStep(clef.clefSign, step)); } // Clef dependent
	int Y(Sign sign, int step) { assert_(sign.type==Sign::Note||sign.type==Sign::Clef); return Y(sign.staff, sign.note.clef, step); } // Clef dependent
	int Y(Sign sign) { assert_(sign.type==Sign::Note); return Y(sign, sign.note.step); } // Clef dependent
	//int Y(const map<uint, Clef>& clefs, uint staff, int step) { return staffY(staff, clefStep(clefs.at(staff).clefSign, step)); } // Clef dependent

    // Fonts
	Font graceFont {File("emmentaler-26.otf", Folder("/usr/local/share/fonts"_)), 4.f*halfLineInterval, "Emmentaler"};
	Font font {File("emmentaler-26.otf", "/usr/local/share/fonts"_), 9.f*halfLineInterval, "Emmentaler"};
	Font textFont{File("LinLibertine_R.ttf", "/usr/share/fonts/libertine-ttf"_), 6.f*halfLineInterval, "LinLibertine_R"};
	Font smallFont{File("LinLibertine_R.ttf", "/usr/share/fonts/libertine-ttf"_), 14.f, "LinLibertine_R"};
    // Font helpers
	vec2 glyphSize(string name) { return font.metrics(font.index(name)).size; }
	vec2 noteSize = glyphSize("noteheads.s2"_);

	// Graphics
	array<int> measures; // X position of measure starts
	shared<Graphics> notation;
	// Graphic helpers
	float glyph(vec2 position, const string name, Font& font);
	float glyph(vec2 position, const string name) { return glyph(position, name, font); }
	uint text(vec2 position, const string& text, Font& font, array<Glyph>& glyphs);
	uint text(vec2 position, const string& text, Font& font) { return this->text(position, text, font, notation->glyphs); }

	int2 sizeHint(int2) override { return int2(measures.last(), -(staffY(1, -32)-staffY(0, 16))); }
	shared<Graphics> graphics(int2 size) override;

	// -- Control
	array<size_t> measureToChord; // First chord index of measure
	array<size_t> chordToNote; // First note index of chord

	/// Returns measure index containing position \a x
	int measureIndex(int x);
	int stop(int unused axis, int currentPosition, int direction) override;

	// -- MIDI Synchronization
	buffer<Sign> midiToSign; /// Sign of corresponding note for each MIDI note
	uint extraErrors = 0, missingErrors = 0, wrongErrors = 0, orderErrors = 0;
	size_t firstSynchronizationFailureChordIndex = -1;

	/// Layouts musical notations to graphic primitives
	Sheet(const ref<Sign>& signs, uint divisions, ref<uint> midiNotes={});
};
