#pragma once
/// \file text.h Rich text label/paragraph widget
#include "widget.h"
#include "function.h"
#include "image.h"
#include "utf8.h"

/// Rich text format control code encoded in 00-1F range
/// \note first word (until ' ') after a Link tag is not displayed but used as \a linkActivated identifier.
enum TextFormat { Regular=0,Bold=1,Italic=2,Underline=4, /*8,'\n','\t'*/ Link=16 };
inline String format(TextFormat f) { String s; s << (char)f; return s; }
inline TextFormat format(uint f) { assert(f<32); return TextFormat(f); }

/// Text is a \a Widget displaying text (can be multiple lines)
struct Text : virtual Widget {
    /// Create a caption that display \a text using a \a size pt (points) font
    Text(const string& text=""_, int size=16, vec4 color=vec4(0,0,0,1), uint wrap=0);
    // Resolves cat overloading
    //Text(const String& text, int size=16, vec4 color=vec4(0,0,0,1), uint wrap=0):Text((string)text,size,color,wrap){}

    void setText(const string& text) { this->text=toUTF32(text); textSize=0; editIndex=min<uint>(editIndex,text.size); }
    void setSize(int size) { this->size=size; textSize=0; }

    /// Displayed text in UTF32
    array<uint> text;
    /// Font size
    int size;
    /// Text color
    vec4 color;
    /// Line wrap limit in pixels (0: no wrap)
    uint wrap=0;
    /// User clicked on this Text
    signal<> textClicked;
    /// User clicked on a \a Format::Link
    signal<const string&> linkActivated;

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
        bool operator ==(const Cursor& o) const { return line==o.line && column==o.column; }
        bool operator <(const Cursor& o) const { return line<o.line || (line==o.line && column<o.column); }
    };
    Cursor cursor; uint editIndex=0;
    uint index();

    // Inline links
    struct Link { Cursor begin,end; String identifier;};
    array<Link> links;
};

/// TextInput is an editable \a Text
struct TextInput : Text {
    TextInput(const string& text=""_, int size=16, vec4 color=vec4(0,0,0,1), uint wrap=0):Text(text,size,color,wrap){} //FIXME: inherit
    /// User edited this text
    signal<const string&> textChanged;
    /// User pressed enter
    signal<const string&> textEntered;
    /// Cursor start position for selections
    Cursor selectionStart;

    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
    bool keyPress(Key key, Modifiers modifiers) override;
    void render(int2 position, int2 size) override;
};
