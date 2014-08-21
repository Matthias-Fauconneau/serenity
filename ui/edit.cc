#include "edit.h"

/// TextInput

bool TextInput::mouseEvent(int2 position, int2 size, Event event, Button button) {
    setCursor(position+Rect(size),::Cursor::Text);
    if(event==Press) setFocus(this);
    if(event==Press && button==MiddleButton) {
        Text::mouseEvent(position,size,event,button);
        array<uint> selection = toUCS4(getSelection());
        if(!text) { editIndex=selection.size; text=move(selection); }
        else { editIndex=index()+selection.size; array<uint> cat; cat<<text.slice(0,index())<<selection<<text.slice(index()); text = move(cat); }
        layout();
        if(textChanged) textChanged(toUTF8(text));
        return true;
    }
    Cursor cursor;
    bool contentChanged = Text::mouseEvent(position,size,event,button) || this->cursor!=cursor;
    if(event==Press && button==LeftButton) { selectionStart = cursor; return true; }
    return contentChanged;
}

bool TextInput::keyPress(Key key, Modifiers modifiers) {
    cursor.line=min<uint>(cursor.line,textLines.size-1);
    const TextLine& textLine = textLines[cursor.line];

    if(modifiers&Control && key=='v') {
        array<uint> selection = toUCS4(getSelection(true));
        if(!text) { text=move(selection); editIndex=selection.size; }
        else { editIndex=index()+selection.size; array<uint> cat; cat<<text.slice(0,index())<<selection<<text.slice(index()); text = move(cat); }
        layout();
        if(textChanged) textChanged(toUTF8(text));
        return true;
    }

    if(key==UpArrow) {
        if(cursor.line>0) cursor.line--;
    } else if(key==DownArrow) {
         if(cursor.line<textLines.size-1) cursor.line++;
    } else {
        cursor.column=min<uint>(cursor.column,textLine.size);

        /**/  if(key==LeftArrow) {
            if(cursor.column>0) cursor.column--;
            else if(cursor.line>0) cursor.line--, cursor.column=textLines[cursor.line].size;
        }
        else if(key==RightArrow) {
            if(cursor.column<textLine.size) cursor.column++;
            else if(cursor.line<textLines.size-1) cursor.line++, cursor.column=0;
        }
        else if(key==Home) cursor.column=0;
        else if(key==End) cursor.column=textLine.size;
        else if(key==Delete) {
            if(cursor.column<textLine.size || cursor.line<textLines.size-1) {
                text.removeAt(editIndex=index()); layout(); if(textChanged) textChanged(toUTF8(text));
            }
        }
        else if(key==Backspace) { //LeftArrow+Delete
            if(cursor.column>0) cursor.column--;
            else if(cursor.line>0) cursor.line--, cursor.column=textLines[cursor.line].size;
            else return false;
            if(index()<text.size) {
                text.removeAt(editIndex=index()); layout(); if(textChanged) textChanged(toUTF8(text));
            }
        }
        else if(key==Return) {
            if(textEntered) textEntered(toUTF8(text));
            else {
                editIndex=index()+1; text.insertAt(index(),'\n'); layout(); if(textChanged) textChanged(toUTF8(text));
            }
        }
        else {
            ref<uint> keypadNumbers = {KP_0, KP_1, KP_2, KP_3, KP_4, KP_5, KP_6, KP_7, KP_8, KP_9};
            char c=0;
            if(key>=' ' && key<=0xFF) c=key; //TODO: UTF8 Compose
            else if(keypadNumbers.contains(key)) c='0'+keypadNumbers.indexOf(key);
            else if(key==KP_Asterisk) c='*'; else if(key==KP_Plus) c='+'; else if(key==KP_Minus) c='-'; else if(key==KP_Slash) c='/';
            else return false;
            editIndex=index()+1; if(text) text.insertAt(index(), c); else text<<c, editIndex=1; layout(); if(textChanged) textChanged(toUTF8(text));
        }
    }
    if(!(modifiers&Shift)) selectionStart=cursor;
    return true;
}

void TextInput::render() {
    Text::render();
    if(hasFocus(this)) {
        assert(cursor.line < textLines.size, cursor.line, textLines.size);
        const TextLine& textLine = textLines[cursor.line];
        int x = 0;
        if(cursor.column<textLine.size) x= textLine[cursor.column].pos.x;
        else if(textLine) x=textLine.last().pos.x+textLine.last().advance;
        int2 offset = max(int2(0),(target.size()-textSize)/2);
        fill(target, offset+int2(x,cursor.line*size)+Rect(2,size), black);
    }
}
