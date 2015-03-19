#pragma once
#include "text.h"

/// TextEdit is an editable \a Text
struct TextEdit : Text {
	struct Cursor {
		size_t line=0, column=0;
		Cursor(){}
		Cursor(size_t line, size_t column) : line(line), column(column) {}
		bool operator ==(const Cursor& o) const { return line==o.line && column==o.column; }
		bool operator <(const Cursor& o) const { return line<o.line || (line==o.line && column<o.column); }
	};
	Cursor cursor;
	float cursorX = 0; // As of last horizontal move to keep horizontal offset constant on vertical moves

	struct State { array<uint> text; Cursor cursor; }; // FIXME: delta
	array<State> history;
	enum class Edit { Point, Delete, Backspace, Insert } lastEdit = Edit::Point;
	size_t historyIndex = 0;

	/// \a Cursor to source text index
	size_t index(Cursor cursor);
	/// Source text index to \a Cursor
	Cursor cursorFromIndex(size_t index);

    /// User edited this text
	function<void(string)> textChanged;
    /// User pressed enter
	function<void(string)> textEntered;
    /// Cursor start position for selections
    Cursor selectionStart;

	TextEdit(buffer<uint>&& text) : Text(move(text), 12, black, 1, 0, "DejaVuSans", true, 1, -1) { history.append({copy(this->text), cursor}); }
	TextEdit(const string text="") : TextEdit(toUCS4(text)) {}
	bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus /*FIXME: -> Window& window*/) override;
    bool keyPress(Key key, Modifiers modifiers) override;
	vec2 cursorPosition(vec2 size, Cursor cursor);
	shared<Graphics> graphics(vec2) override;
};
