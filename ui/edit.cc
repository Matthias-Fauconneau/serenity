#include "edit.h"

/*generic buffer<ref<T>> toRefs(ref<array<T>> arrays) { return apply(arrays, [](const array<T>& e) -> ref<T> { return e; }); }
generic buffer<T> join(ref<ref<T>> list, ref<T> separator) {
	if(!list) return {};
	size_t size = 0; for(const auto& e: list) size += e.size;
	buffer<T> buffer (size + (list.size-1)*separator.size, 0);
	for(size_t index: range(list.size)) { buffer.append( list[index] ); if(index<list.size-1) buffer.append( separator ); }
	return buffer;
}*/

struct EditStop { float left, center, right; size_t sourceIndex; };
array<EditStop> lineStops(ref<array<TextLayout::Glyph>> line) {
	array<EditStop> stops;
	for(const auto& word: line) {
		if(stops) { // Justified space
			float left = stops.last().right, right = word[0].origin.x - word[0].bearing.x, center = (left+right)/2;
			assert_(stops.last().sourceIndex+1 == word[0].sourceIndex-1);
			//assert_(text[stops.last().sourceIndex+1]==' ');
			stops.append({left, center, right, stops.last().sourceIndex+1});
		}
		for(auto& glyph: word) {
			float left = glyph.origin.x - glyph.bearing.x, right = left + glyph.advance, center = (left+right)/2;
			stops.append({left, center, right, glyph.sourceIndex});
		}
	}
	return stops;
}

/// TextEdit
bool TextEdit::mouseEvent(vec2 position, vec2 size, Event event, Button button, Widget*& focus) {
	setCursor(::Cursor::Text);
	focus=this;
	bool cursorChanged = false;
	if(event==Press || (event==Motion && button)) {
		const TextLayout& layout = this->layout(size.x ? min<float>(wrap, size.x) : wrap);
		vec2 textSize = ceil(layout.bbMax - min(vec2(0),layout.bbMin));
		vec2 offset = max(vec2(0), vec2(align==0 ? size.x/2 : (size.x-textSize.x)/2.f, (size.y-textSize.y)/2.f));
		position -= offset;
		if(!Rect(textSize).contains(position)) return false;
		Cursor cursor = this->cursor;
		for(size_t lineIndex: range(layout.glyphs.size)) {
			if(position.y < (lineIndex*this->size) || position.y > (lineIndex+1)*this->size) continue; // FIXME: Assumes uniform line height
			const auto line = lineStops(layout.glyphs[lineIndex]);
			if(!line) continue;
			// Before first stop
			const auto& first = line[0];
			if(position.x <= first.center) { cursor = Cursor(lineIndex, 0); break; }
			// Between stops
			for(size_t stop: range(0, line.size-1)) {
				if(position.x >= line[stop].center && position.x <= line[stop+1].center) { cursor = Cursor(lineIndex, stop+1); goto break2;/*break 2;*/ }
			}
			// After last stop
			if(position.x >= line.last().center) { cursor = Cursor(lineIndex, line.size); break; }
		}break2:;
		cursorChanged = (cursor != this->cursor);
		this->cursor = cursor;
	}
    if(event==Press && button==LeftButton) { selectionStart = cursor; return true; }
	/*if(event==Release && button==MiddleButton) {
		Text::mouseEvent(position, size, event, button, focus);
		array<uint32> selection = toUCS4(getSelection());
		if(!text) { sourceIndex=selection.size; text=move(selection); }
		else { sourceIndex=index()+selection.size; array<uint> cat; cat<<text.slice(0,index())<<selection<<text.slice(index()); text = move(cat); }
		layout();
		if(textChanged) textChanged(toUTF8(text));
		return true;
	}*/
	return cursorChanged;
}

size_t TextEdit::index() {
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
	return text.size;
	/*size_t index = 1; // ' ', '\t' or '\n' immediately after last glyph
	size_t line = cursor.line;
	while(line>0 && !lines[line]) line--, index++; //count \n (not included as glyphs)
	if(lines[line]) index += lines[line].last().sourceIndex;
	return index;*/
}

bool TextEdit::keyPress(Key key, Modifiers modifiers) {
	const auto& lines = lastTextLayout.glyphs;
	cursor.line = min<size_t>(cursor.line, lines.size-1);
	const auto line = lineStops(lines[cursor.line]);

	/*if(modifiers&Control && key=='v') {
        array<uint> selection = toUCS4(getSelection(true));
		if(!text) { text=move(selection); sourceIndex=selection.size; }
		else { sourceIndex=index()+selection.size; array<uint> cat; cat<<text.slice(0,index())<<selection<<text.slice(index()); text = move(cat); }
        layout();
        if(textChanged) textChanged(toUTF8(text));
        return true;
	}*/

    if(key==UpArrow) {
        if(cursor.line>0) cursor.line--;
    } else if(key==DownArrow) {
		 if(cursor.line<lines.size-1) cursor.line++;
    } else {
		cursor.column = min<size_t>(cursor.column, line.size);

        /**/  if(key==LeftArrow) {
            if(cursor.column>0) cursor.column--;
			else if(cursor.line>0) cursor.line--, cursor.column = lines[cursor.line].size;
        }
        else if(key==RightArrow) {
			if(cursor.column<line.size) cursor.column++;
			else if(cursor.line<lines.size-1) cursor.line++, cursor.column = 0;
        }
		else if(key==Home) cursor.column = 0;
		else if(key==End) cursor.column = line.size;
        else if(key==Delete) {
			if(cursor.column<line.size || cursor.line<lines.size-1) {
				text.removeAt(index());
				lastTextLayout = TextLayout();
				if(textChanged) textChanged(toUTF8(text));
            }
        }
        else if(key==Backspace) { //LeftArrow+Delete
            if(cursor.column>0) cursor.column--;
			else if(cursor.line>0) cursor.line--, cursor.column=lines[cursor.line].size;
            else return false;
            if(index()<text.size) {
				text.removeAt(index());
				lastTextLayout = TextLayout();
				if(textChanged) textChanged(toUTF8(text));
            }
        }
        else if(key==Return) {
            if(textEntered) textEntered(toUTF8(text));
            else {
				text.insertAt(index(), uint32('\n'));
				lastTextLayout = TextLayout();
				if(textChanged) textChanged(toUTF8(text));
            }
        }
        else {
            ref<uint> keypadNumbers = {KP_0, KP_1, KP_2, KP_3, KP_4, KP_5, KP_6, KP_7, KP_8, KP_9};
            char c=0;
            if(key>=' ' && key<=0xFF) c=key; //TODO: UTF8 Compose
            else if(keypadNumbers.contains(key)) c='0'+keypadNumbers.indexOf(key);
            else if(key==KP_Asterisk) c='*'; else if(key==KP_Plus) c='+'; else if(key==KP_Minus) c='-'; else if(key==KP_Slash) c='/';
            else return false;
			text.insertAt(index(), uint32(c));
			lastTextLayout = TextLayout();
			if(textChanged) textChanged(toUTF8(text));
        }
    }
    if(!(modifiers&Shift)) selectionStart=cursor;
    return true;
}

shared<Graphics> TextEdit::graphics(vec2 size) {
	shared<Graphics> graphics = Text::graphics(size);
	/* Enforces valid cursor position
	   cursor=Cursor(0,0); uint currentIndex=0;
	   for(const auto& line: layout.text) {
		   TextLine line;
		   for(const TextLayout::Glyph& o: line) {
			   currentIndex = o.sourceIndex;
			   if(currentIndex<=sourceIndex) { // Restores cursor after relayout
				   cursor = Cursor(textLines.size, line.size);
			   }
			   line << ;
		   }
		   currentIndex++;
		   if(currentIndex<=sourceIndex) cursor = Cursor(textLines.size, line.size); // End of line
		   textLines << move(line);
	   }
	   if(!text.size) { assert(sourceIndex==0); cursor = Cursor(0,0); }
	   else if(currentIndex<=sourceIndex) { assert(textLines); cursor = Cursor(textLines.size-1, textLines.last().size); } // End of text*/

	if(hasFocus(this)) {
		const auto& lines = lastTextLayout.glyphs;
		assert(cursor.line < lines.size, cursor.line, textLines.size);
		const auto line = lineStops(lines[cursor.line]);
		float x = 0;
		if(cursor.column<line.size) x = line[cursor.column].left;
		else if(line) x = line.last().right;
		vec2 textSize = ceil(lastTextLayout.bbMax - min(vec2(0), lastTextLayout.bbMin));
		vec2 offset = max(vec2(0), vec2(align==0 ? size.x/2 : (size.x-textSize.x)/2.f, (size.y-textSize.y)/2.f));
		graphics->lines.append(offset+vec2(x,cursor.line*Text::size), offset+vec2(x,(cursor.line+1)*Text::size)); // Assumes uniform line heights
    }
	return graphics;
}
