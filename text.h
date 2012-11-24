#pragma once
/// \file text.h Rich text label/paragraph widget
#include "widget.h"
#include "function.h"
#include "image.h"
#include "utf8.h"

/// Rich text format control code encoded in 00-1F range
/// \note first word (until ' ') after a Link tag is not displayed but used as \a linkActivated identifier.
enum Format { Regular=0,Bold=1,Italic=2,Underline=4, /*8,'\n','\t'*/ Link=16 };
inline string format(Format f) { assert(f<32); string s; s << (char)f; return s; }
inline Format format(uint f) { assert(f<32); return Format(f); }

/// Text is a \a Widget displaying text (can be multiple lines)
struct Text : Widget {
    /// Create a caption that display \a text using a \a size pt (points) font
    Text(const ref<byte>& text=""_, int size=16, uint8 opacity=255, uint wrap=0);
    // Resolves cat overloading
    Text(const string& text, int size=16, uint8 opacity=255, uint wrap=0):Text((ref<byte>)text,size,opacity,wrap){}

    void setText(ref<byte> text) { this->text=toUTF32(text); textSize=0; editIndex=min(editIndex,text.size); }
    void setSize(int size) { this->size=size; textSize=0; }

    /// Displayed text in UTF32
    array<uint> text;
    /// Font size
    int size;
    /// Opacity
    uint8 opacity;
    /// Line wrap limit in pixels (0: no wrap)
    uint wrap=0;
    /// User clicked on this Text
    signal<> textClicked;
    /// User clicked on a \a Format::Link
    signal<const ref<byte>&> linkActivated;

    int2 sizeHint();
    void layout();
    void render(int2 position, int2 size=int2(0,0)) override;
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;

    // Layout bounding box
    int2 textSize=0;
    // Minimal size hint
    int2 minSize=0;

    // Characters to render
    struct Character { int2 pos; Image image; uint editIndex; int center,height,advance;};
    typedef array<Character> TextLine;
    array<TextLine> textLines;

    // Underlines and strikes
    struct Line { int2 min,max; };
    array<Line> lines;

    // Cursor
    struct Cursor {
        uint line=0,column=0;
        Cursor(){}
        Cursor(uint line, uint column):line(line),column(column){}
        bool operator >(const Cursor& o)const{return line>o.line || (line==o.line && column>o.column);}
    };
    Cursor cursor; uint editIndex=0;
    Character& current() {
        assert(cursor.line<textLines.size());
        assert(cursor.column<textLines[cursor.line].size());
        return textLines[cursor.line][cursor.column];
    }
    uint index() {
        if(!textLines) return 0;
        if(cursor.line==textLines.size()) return textLines.last().last().editIndex;
        assert(cursor.line<textLines.size(),cursor.line,textLines.size());
        assert(cursor.column<=textLines[cursor.line].size(), cursor.column, textLines[cursor.line].size());
        if(cursor.column<textLines[cursor.line].size()) {
            uint index = textLines[cursor.line][cursor.column].editIndex;
            assert(index<text.size());
            return index;
        }
        uint index = 1; // ' ', '\t' or '\n' immediatly after last character
        uint line=cursor.line;
        while(line>0 && !textLines[line]) line--, index++; //count \n (not included as characters)
        if(textLines[line]) index += textLines[line].last().editIndex;
        //assert(index<=text.size());
        return index;
    }

    // Inline links
    struct Link { Cursor begin,end; string identifier;};
    array<Link> links;
};

/// TextInput is an editable \a Text
struct TextInput : Text {
    TextInput(const ref<byte>& text=""_, int size=16, uint8 opacity=255, uint wrap=0):Text(text,size,opacity,wrap){} //FIXME: inherit
    /// User edited this text
    signal<const ref<byte>&> textChanged;
    /// User pressed enter
    signal<const ref<byte>&> textEntered;
    /// Last cursor position
    Cursor last;

    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
    bool keyPress(Key key) override;
    void render(int2 position, int2 size) override;
};
