#pragma once
#include "text.h"

/// TextEdit is an editable \a Text
struct TextEdit : Text {
	// Cursor
	struct Cursor {
		size_t line=0, column=0;
		Cursor(){}
		Cursor(size_t line, size_t column) : line(line), column(column) {}
		bool operator ==(const Cursor& o) const { return line==o.line && column==o.column; }
		bool operator <(const Cursor& o) const { return line<o.line || (line==o.line && column<o.column); }
	};
	Cursor cursor;
	float cursorX = 0; // As of last horizontal move to keep horizontal offset constant on vertical moves
	size_t editIndex=0;

	size_t index();
    /// User edited this text
	function<void(string)> textChanged;
    /// User pressed enter
	function<void(string)> textEntered;
    /// Cursor start position for selections
    Cursor selectionStart;

	TextEdit(const string text="") : Text(text, 16, black, 1, 0, "DejaVuSans", true, 1, -1) {}
	bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus /*FIXME: -> Window& window*/) override;
    bool keyPress(Key key, Modifiers modifiers) override;
	shared<Graphics> graphics(vec2) override;
};
