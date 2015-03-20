#pragma once
#include "text.h"

/// TextEdit is an editable \a Text
struct TextEdit : Text {
	Cursor cursor;
	float cursorX = 0; // As of last horizontal move to keep horizontal offset constant on vertical moves

	struct State { array<uint> text; Cursor cursor; }; // FIXME: delta
	array<State> history;
	enum class Edit { Point, Delete, Backspace, Insert } lastEdit = Edit::Point;
	size_t historyIndex = 0;
	array<Cursor> cursorHistory;

	/// \a Cursor to source text index
	size_t index(Cursor cursor) const;
	/// Source text index to \a Cursor
	Cursor cursorFromIndex(size_t index) const;
	Cursor cursorFromPosition(vec2 size, vec2 position);

    /// User edited this text
	function<void(string)> textChanged;
    /// User pressed enter
	function<void(string)> textEntered;
	/// User pressed enter
	function<void(ref<uint>)> linkActivated;
	/// User pressed enter
	function<void()> back;
    /// Cursor start position for selections
    Cursor selectionStart;

	Rect lastClip {0};

	TextEdit(buffer<uint>&& text) : Text(move(text), 12, black, 1, 0, "DejaVuSans", true, 1, -1) { history.append({copy(this->text), cursor}); }
	TextEdit(const string text="") : TextEdit(toUCS4(text)) {}

	void previousWord();
	void nextWord();

	bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus /*FIXME: -> Window& window*/) override;
    bool keyPress(Key key, Modifiers modifiers) override;
	vec2 cursorPosition(vec2 size, Cursor cursor);
	shared<Graphics> graphics(vec2 size, Rect clip) override;
};
