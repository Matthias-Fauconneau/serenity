#include "edit.h"

/// Strips text format markers
buffer<uint32> strip(ref<uint32> source) {
	buffer<uint32> target(source.size, 0);
	for(size_t index=0; index<source.size; index++) {
		uint32 c = source[index];
		if(TextFormat(c)>=TextFormat::Begin && TextFormat(c)<TextFormat::End) {
			TextFormat format = TextFormat(c);
			if(format==TextFormat::Bold || format==TextFormat::Italic || format==TextFormat::Subscript || format==TextFormat::Superscript) {}
			else if(format==TextFormat::Color) index += 3;
			else if(format==TextFormat::Link) { index++; while(source[index]) index++; }
			else error("Unknown format", uint(format));
		}
		else if(TextFormat(c)==TextFormat::End) {}
		else {
			assert_(c, source);
			target.append(c);
		}
	}
	return target;
}

struct EditStop { float left, center, right; size_t sourceIndex; };
array<EditStop> lineStops(ref<array<TextLayout::Glyph>> line) {
	array<EditStop> stops;
	for(const auto& word: line) {
		if(stops) { // Justified space
			float left = stops.last().right, right = word[0].origin.x, center = (left+right)/2;
			assert_(stops.last().sourceIndex+1 == word[0].sourceIndex-1);
			//assert_(text[stops.last().sourceIndex+1]==' ');
			stops.append({left, center, right, stops.last().sourceIndex+1});
		}
		for(auto& glyph: word) {
			float left = glyph.origin.x, right = left + glyph.advance, center = (left+right)/2;
			assert_(right > left);
			stops.append({left, center, right, glyph.sourceIndex});
		}
	}
	return stops;
}

size_t TextEdit::index(Cursor cursor) const {
	const auto& lines = lastTextLayout.glyphs;
	if(!lines) return 0;
	if(cursor.line==lines.size) return lines.last().last().last().sourceIndex;
	assert(cursor.line<lines.size, cursor.line, lines.size);
	const auto line = lineStops(lines[cursor.line]);
	assert(cursor.column <= line.size, cursor.column, line.size);
	if(cursor.column < line.size) {
		size_t sourceIndex = line[cursor.column].sourceIndex;
		assert(sourceIndex < text.size);
		return sourceIndex;
	}
	if(!lines) return 0;
	size_t index = 1; // ' ', '\t' or '\n' immediately after last glyph
	size_t lineIndex = cursor.line;
	while(lineIndex>0 && !lines[lineIndex]) lineIndex--, index++; // Seeks last glyph backwards counting line feeds (not included as glyphs)
	if(lines[lineIndex]) index += lineStops(lines[lineIndex]).last().sourceIndex;
	return index;
}

Cursor TextEdit::cursorFromIndex(size_t targetIndex) const {
	const auto& lines = lastTextLayout.glyphs;
	Cursor cursor (0, 0);
	if(!lines) return cursor;
	size_t lastIndex = 0;
	for(size_t lineIndex: range(lines.size)) {
		cursor.column = 0;
		const auto line = lineStops(lines[lineIndex]);
		for(auto o: line) {
			lastIndex = o.sourceIndex;
			if(lastIndex >= targetIndex) return cursor;
			cursor.line = lineIndex;
			cursor.column++;
		}
		lastIndex++;
		if(lastIndex>=targetIndex) return cursor;
		cursor.column = line.size; // End of line
	}
	if(lastIndex>=targetIndex) return cursor;
	return Cursor(lines.size-1, lineStops(lines.last()).size);  // End of text
}

Cursor TextEdit::cursorFromPosition(vec2 size, vec2 position) {
	const TextLayout& layout = this->layout(size.x ? min<float>(wrap, size.x) : wrap);
	vec2 textSize = ceil(layout.bbMax - min(vec2(0),layout.bbMin));
	vec2 offset = max(vec2(0), vec2(align==0 ? size.x/2 : (size.x-textSize.x)/2.f, (size.y-textSize.y)/2.f));
	position -= offset;
	if(position.y < 0) return {0, 0};
	const auto& lines = lastTextLayout.glyphs;
	for(size_t lineIndex: range(lines.size)) {
		if(position.y < (lineIndex*this->size) || position.y > (lineIndex+1)*this->size) continue; // FIXME: Assumes uniform line height
		const auto line = lineStops(lines[lineIndex]);
		if(!line) return {lineIndex, line.size};
		// Before first stop
		if(position.x <= line[0].center) return {lineIndex, 0};
		// Between stops
		for(size_t stop: range(0, line.size-1)) {
			if(position.x >= line[stop].center && position.x <= line[stop+1].center) return {lineIndex, stop+1};
		}
		// After last stop
		if(position.x >= line.last().center) return {lineIndex, line.size};
	}
	return {lines.size-1, lineStops(lines.last()).size};
}

/// TextEdit
bool TextEdit::mouseEvent(vec2 position, vec2 size, Event event, Button button, Widget*& focus) {
	setCursor(MouseCursor::Text);
	focus = this;
	Cursor previousCursor = this->cursor;
	if((event==Press && (button==LeftButton || button==RightButton)) || (event==Motion && button==LeftButton)) {
		cursor = cursorFromPosition(size, position);
	}
	if(event==Release) {
		if(button==LeftButton) {
			Cursor min, max;
			if(selectionStart < cursor) min=selectionStart, max=cursor; else min=cursor, max=selectionStart;
			if(cursor != selectionStart) setSelection(toUTF8(strip(text.sliceRange(index(min),index(max)))), false);
			else for(const Link& link: lastTextLayout.links) {
				if(link.begin<=cursor && cursor<link.end) { cursorHistory.append(cursor); linkActivated(link.identifier); break; }
			}
		}
		if(button==MiddleButton) {
			array<uint32> selection = toUCS4(getSelection(false));
			size_t index = this->index(cursor);
			text = buffer<uint>(text.slice(0,index) + selection + text.slice(index));
			lastTextLayout = TextLayout();
			if(textChanged) textChanged(text);
			this->layout(size.x ? min<float>(wrap, size.x) : wrap);
			cursor = cursorFromIndex(index+selection.size);
			return true;
		}
	}
	if(cursor == previousCursor) return false;
	/*if(!(modifiers&Shift)) FIXME*/ selectionStart = cursor;
	history.last().cursor = cursor;
	lastEdit=Edit::Point;
	return true;
}


static bool isWordCode(char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||"_"_.contains(c); }
void TextEdit::previousWord() {
	cursor.column--;
	bool firstWordCode = isWordCode(text[index(cursor)]);
	while(cursor.column>0 && isWordCode(text[index({cursor.line, cursor.column-1})]) == firstWordCode) cursor.column--;
}
void TextEdit::nextWord() {
	bool firstWordCode = isWordCode(text[index(cursor)]);
	const auto& lines = lastTextLayout.glyphs;
	auto line = lineStops(lines[cursor.line]);
	while(cursor.column<line.size && isWordCode(text[index(cursor)]) == firstWordCode) cursor.column++;
}

bool TextEdit::keyPress(Key key, Modifiers modifiers) {
	const auto& lines = lastTextLayout.glyphs;
	cursor.line = min<size_t>(cursor.line, lines.size-1);
	auto line = lineStops(lines[cursor.line]);

	if( ( (modifiers&Control) && (key==Home || key==End)) ||
		(!(modifiers&Control) && (key==UpArrow || key==DownArrow || key==PageUp || key==PageDown))) {
		int lineIndex = cursor.line;
		if(key==UpArrow) lineIndex--;
		else if(key==DownArrow) lineIndex++;
		else if(key==PageUp || key==PageDown) {
			const int pageLineCount = lastClip.size().y / size; // Assumes uniform line size
			if(key==PageUp) lineIndex -= pageLineCount/2;
			if(key==PageDown) lineIndex += pageLineCount/2;
		}
		else if(key==Home) lineIndex = 0;
		else if(key==End) lineIndex = lines.size-1;
		else error("");
		lineIndex = clamp(0, lineIndex, int(lines.size-1));
		if(lineIndex == int(cursor.line)) return false;
		cursor.line = lineIndex;
		const auto line = lineStops(lines[cursor.line]);
		if(line) {
			if(!cursorX) cursorX = cursor.column<line.size ? line[cursor.column].left : line ? line.last().right : 0;
			for(size_t stop: range(0, line.size-1)) if(cursorX >= line[stop].center && cursorX <= line[stop+1].center) { cursor.column = stop+1; break; }
			if(cursorX >= line.last().center) cursor.column = line.size;
		}
		if(!(modifiers&Shift)) selectionStart = cursor;
		history.last().cursor = cursor;
		lastEdit=Edit::Point;
		return true;
	} else if(key==LeftArrow || key==RightArrow || key==Home || key==End) {
		cursor.column = min<size_t>(cursor.column, line.size);

		if(key==Home) cursor.column = 0;
		else if(key==End) cursor.column = line.size;
		else if((modifiers&Control) && ((key==LeftArrow && cursor.column>0) || (key==RightArrow && cursor.column<line.size))) {
			if(key==LeftArrow) previousWord();
			else if(key==RightArrow) nextWord();
			else error("");
		}
		else if(key==LeftArrow)  {
			if(modifiers&Alt) { // Back
				if(modifiers&Shift) { back(); }
				else { if(cursorHistory) cursor = cursorHistory.pop(); }
			} else {
				if(cursor.column>0) cursor.column--;
				else if(cursor.line>0) { cursor.line--, cursor.column = lineStops(lines[cursor.line]).size; }
			}
		}
		else if(key==RightArrow) {
			if(modifiers&Alt) { // Forward
				for(const Link& link: lastTextLayout.links) {
					if(link.begin<=cursor && cursor<link.end) { cursorHistory.append(cursor); linkActivated(link.identifier); break; }
				}
			} else {
				if(cursor.column<line.size) cursor.column++;
				else if(cursor.line<lines.size-1) cursor.line++, cursor.column = 0;
			}
		}
		else error("");
		if(!(modifiers&Shift)) selectionStart = cursor;
		history.last().cursor = cursor;
		lastEdit=Edit::Point;
		return true;
	}

	if((modifiers&Control) && (modifiers&Shift) && (key=='z'||key=='Z')) {
		if(!(historyIndex<history.size-1)) return false;
		historyIndex++;
		const State& state = history[historyIndex];
		text = copy(state.text);
		selectionStart = cursor = state.cursor;
		lastTextLayout = TextLayout();
		if(textChanged) textChanged(text);
		lastEdit=Edit::Point;
		return true;
	}

	if((modifiers&Control) && (key=='z'||key=='Z')) {
		if(!historyIndex) return false;
		historyIndex--;
		const State& state = history[historyIndex];
		text = copy(state.text);
		selectionStart = cursor = state.cursor;
		lastTextLayout = TextLayout();
		if(textChanged) textChanged(text);
		lastEdit=Edit::Point;
		return true;
	}

	if((modifiers&Control) && (key=='c'||key=='C'||key=='x'||key=='X')) {
		Cursor min, max;
		if(selectionStart < cursor) min=selectionStart, max=cursor; else min=cursor, max=selectionStart;
		if(cursor != selectionStart) setSelection(toUTF8(strip(text.sliceRange(index(min),index(max)))), true);
		if((key=='c'||key=='C') || cursor == selectionStart) return true;
	}

	Edit edit = Edit::Point;
	if((modifiers&Control) && (key=='v'||key=='V')) {
        array<uint> selection = toUCS4(getSelection(true));
		size_t index = this->index(cursor);
		text = buffer<uint>(text.slice(0,index) + selection + text.slice(index));
		float wrap = lastTextLayout.wrap;
		lastTextLayout = TextLayout();
		this->layout(wrap);
		cursor = cursorFromIndex(index+selection.size);
	} else {
		if(modifiers&Control) {
			if(key==Delete) nextWord();
			if(key==Backspace) previousWord();
		}
		Cursor min, max;
		if(selectionStart < cursor) min=selectionStart, max=cursor; else min=cursor, max=selectionStart;
		size_t sourceIndex = index(min);
		if(key==Delete || key==Backspace || key==Return || key==KP_Enter || ((modifiers&Control) && (key=='x'||key=='X'))) {
			if(cursor != selectionStart) { // Deletes selection
				text = buffer<uint>(text.slice(0, index(min)) + text.slice(index(max)));
				cursor = min;
			} else {
				if(key==Delete) {
					edit = Edit::Delete;
					if(cursor.column<line.size || cursor.line<lines.size-1) text.removeAt(sourceIndex);
					else return false;
				}
				else if(key==Backspace) { // <=> LeftArrow, Delete
					edit = Edit::Backspace;
					if(cursor.column>0) cursor.column--;
					else if(cursor.line>0) cursor.line--, cursor.column=lineStops(lines[cursor.line]).size;
					else return false;
					if(cursor.column<line.size || cursor.line<lines.size-1) text.removeAt(sourceIndex);
					else return false;
				}
				else if(key==Return || key==KP_Enter) {
					if(textEntered) { textEntered(text); return false; }
					text.insertAt(sourceIndex, uint32('\n'));
					cursor.line++; cursor.column = 0;
				}
				else error("");
			}
		} else {
			char c;
			if(key>=' ' && key<=0xFF) c=key; // FIXME: Unicode
			else if(key >= KP_Asterisk && key<=KP_9) c="*+.-./0123456789"_[key-KP_Asterisk];
			else return false;
			edit = Edit::Insert;
			if(cursor != selectionStart) { // Deletes selection
				text = buffer<uint>(text.slice(0, index(min)) + text.slice(index(max)));
				cursor = min;
				line = lineStops(lines[cursor.line]);
			}
			text.insertAt(index(cursor), uint32(c));
			cursor.column++;
		}
		lastTextLayout = TextLayout();
	}

	if(textChanged) textChanged(text);
	selectionStart = cursor;
	cursorX = 0;

	// Records history
	if(historyIndex > history.size) history.shrink(historyIndex);
	if(edit!=Edit::Point && edit == lastEdit) history.last() = {copy(text), cursor};
	else history.append({copy(text), cursor});
	historyIndex = history.size-1;
	lastEdit=edit;
	return true;
}

vec2 TextEdit::cursorPosition(vec2 size, Cursor cursor) {
	const TextLayout& layout = this->layout(size.x ? min<float>(wrap, size.x) : wrap);
	vec2 textSize = ceil(layout.bbMax - min(vec2(0), layout.bbMin));
	vec2 offset = max(vec2(0), vec2(align==0 ? size.x/2 : (size.x-textSize.x)/2.f, (size.y-textSize.y)/2.f));
	const auto line = lineStops(layout.glyphs[cursor.line]);
	float x = cursor.column<line.size ? line[cursor.column].left : line ? line.last().right : 0;
	return offset+vec2(x,cursor.line*Text::size); // Assumes uniform line heights
}

shared<Graphics> TextEdit::graphics(vec2 size, Rect clip) {
	lastClip = clip;
	shared<Graphics> graphics = Text::graphics(size);

	{Cursor min, max;
		if(selectionStart < cursor) min=selectionStart, max=cursor; else min=cursor, max=selectionStart;
		if(selectionStart.line == cursor.line) { // Single line selection
			vec2 O = cursorPosition(size, min); graphics->fills.append(O, cursorPosition(size, max)-O+vec2(0,Text::size), black, 1.f/2);
		} else { // Multiple line selection
			{vec2 O = cursorPosition(size, {min.line, min.column});
				graphics->fills.append(O, cursorPosition(size, {min.line, invalid})-O+vec2(0,Text::size), black, 1.f/2);}
			for(size_t line: range(min.line+1, max.line)) {
				vec2 O = cursorPosition(size, {line, 0}); graphics->fills.append(O, cursorPosition(size, {line, invalid})-O+vec2(0,Text::size), black, 1.f/2);
			}
			{vec2 O = cursorPosition(size, {max.line, 0});
				graphics->fills.append(O, cursorPosition(size, {max.line, max.column})-O+vec2(0,Text::size), black, 1.f/2);}
		}
	}
	/*if(hasFocus(this))*/ graphics->fills.append(cursorPosition(size, cursor), vec2(1,Text::size));
	return graphics;
}
