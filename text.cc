#include "text.h"
#include "display.h"
#include "font.h"

map<int,Font> defaultSans;
map<int,Font> defaultBold;
map<int,Font> defaultItalic;
map<int,Font> defaultMono;

/// Layouts formatted text with wrapping, justification and links
/// \note Characters are positioned with .4 subpixel precision
struct TextLayout {
    float size;
    float wrap;
    float spaceAdvance;
    vec2 pen=0;
    struct Character { Font* font; vec2 pos; uint index; uint width; float advance; uint editIndex; };
    typedef array<Character> Word;
    array<Word> line;
    typedef array<Character> TextLine;
    array<TextLine> text;
    struct Line { Text::Cursor begin,end; };
    array<Line> lines;
    array<Text::Link> links;

    uint lineNumber=0,column=0;
    Text::Cursor current() { return Text::Cursor __(lineNumber, column); }

    void nextLine(bool justify) {
        //if(!line) { pen.y+=size; return; }
        //justify
        float length=0; for(const Word& word: line) { if(word) { length+=word.last().pos.x+word.last().advance; } } //sum word length
        if(line && line.last()) length += line.last().last().width - line.last().last().advance; //for last word of line, use glyph bound instead of advance
        float space=0;
        if(justify && line.size()>1) space = (wrap-length)/(line.size()-1);
        if(space<=0 || space>=2*spaceAdvance) space = spaceAdvance; //compact

        //layout
        column=0; pen.x=0;
        lineNumber++; text << TextLine(); uint lastIndex=-1;
        for(uint i: range(line.size())) { Word& word=line[i];
            //assert(word);
            for(Character& c: word) text.last() << Character __(c.font, pen+c.pos, c.index, 0, c.advance, lastIndex=c.editIndex);
            if(word) pen.x += word.last().pos.x+word.last().advance;
            if(i!=line.size()-1) text.last() << Character __(0,pen+vec2(0,-this->size),0,0,spaceAdvance,lastIndex+1); //editable justified space
            pen.x += space;
        }
        line.clear();
        pen.x=0; pen.y+=size;
    }

    TextLayout(const ref<uint>& text, int size, int wrap, Font* font=0):size(size),wrap(wrap) {
        if(!font) {
            if(!defaultSans.contains(size)) defaultSans.insert(size,Font(File("dejavu/DejaVuSans.ttf"_,fonts()), size));
            font = &defaultSans.at(size);
        }
        uint16 spaceIndex = font->index(' ');
        spaceAdvance = font->advance(spaceIndex); assert(spaceAdvance);
        uint16 previous=spaceIndex;
        Format format=Regular;
        Text::Link link;
        Text::Cursor underlineBegin;
        Word word;
        pen.y = font->ascender;
        for(uint i=0; i<text.size; i++) {
            uint c = text[i];
            if(c==' '||c=='\t'||c=='\n') { //next word/line
                previous = spaceIndex;
                float length=0; for(const Word& word: line) if(word) length+=word.last().pos.x+word.last().advance+spaceAdvance;
                if(word) length += word.last().pos.x+word.last().width; //last word
                if(wrap && length>=wrap && line) nextLine(true); //doesn't fit
                line << move(word); //add to current line (or first of new line)
                pen.x=0;
                if(c=='\n') nextLine(false);
                continue;
            }
            if(c<0x20) { //00-1F format control flags (bold,italic,underline,strike,link)
                if(format&Link) {
                    link.end=current();
                    links << move(link);
                }
                Format newFormat = ::format(c);
                if(format&Underline && !(newFormat&Underline) && (current()>underlineBegin))
                    lines << Line __(underlineBegin, current());
                format=newFormat;
                if(format&Bold) {
                    if(!defaultBold.contains(size)) defaultBold.insert(size,Font(File("dejavu/DejaVuSans-Bold.ttf"_,fonts()), size));
                    font = &defaultBold.at(size);
                } else if(format&Italic) {
                    if(!defaultItalic.contains(size)) defaultItalic.insert(size,Font(File("dejavu/DejaVuSans-Oblique.ttf"_,fonts()), size));
                    font = &defaultItalic.at(size);
                } else {
                    if(!defaultSans.contains(size)) defaultSans.insert(size,Font(File("dejavu/DejaVuSans.ttf"_,fonts()), size));
                    font = &defaultSans.at(size);
                }
                if(format&Underline) underlineBegin=current();
                if(format&Link) {
                    for(;;) {
                        i++; assert(i<text.size);
                        uint c = text[i];
                        if(c == ' ') break;
                        link.identifier << utf8(c);
                    }
                    link.begin = current();
                }
                continue;
            }
            uint16 index = font->index(c);
            if(previous!=spaceIndex) pen.x += font->kerning(previous,index);
            previous = index;
            float advance = font->advance(index);
            const Image& image = font->glyph(index).image;
            if(image) { word << Character __(font, vec2(pen.x,0), index, image.width, advance, i); column++; }
            pen.x += advance;
        }
        float length=0; for(const Word& word: line) if(word) length+=word.last().pos.x+word.last().advance+spaceAdvance;
        if(word) length += word.last().pos.x+word.last().width; //last word
        if(wrap && length>=wrap) nextLine(true); //doesn't fit
        line << move(word); //add to current line (or first of new line)
        pen.x=0;
        nextLine(false);
    }
};

Text::Text(const ref<byte>& text, int size, uint8 opacity, uint wrap) : text(toUTF32(text)), size(size), opacity(opacity), wrap(wrap) {}
void Text::layout() {
    textSize=int2(0,size);
    TextLayout layout(text, size, wrap);

    textLines.clear(); textLines.reserve(layout.text.size());
    debug( cursor.line = -1, cursor.column=-1;  ) uint currentIndex=0;
    for(const TextLayout::TextLine& line: layout.text) {
        TextLine textLine;
        for(const TextLayout::Character& o: line) {
            currentIndex = o.editIndex;
            if(currentIndex==editIndex) { //restore cursor after relayout
                cursor = Cursor __(textLines.size(), textLine.size());
            }
            if(o.font) {
                const Glyph& glyph=o.font->glyph(o.index,o.pos.x);
                Character c __(int2(o.pos)+glyph.offset, share(glyph.image), o.editIndex, int(o.pos.x+o.advance/2), (int)glyph.image.height, int(o.advance));
                textSize=max(textSize,int2(c.pos)+c.image.size());
                textLine << move(c);
            } else { //format character
                textLine << Character __(int2(o.pos),Image(),o.editIndex,int(o.pos.x+o.advance/2), this->size, int(o.advance));
            }
        }
        currentIndex++;
        if(currentIndex==editIndex) cursor = Cursor __(textLines.size(), textLine.size()); //end of line
        textLines << move(textLine);
    }
    if(!text.size()) { assert(editIndex==0); cursor = Cursor __(0,0); }
    else if(currentIndex==editIndex) cursor = Cursor __(textLines.size()-1, textLines.last().size()); //end of text
    links = move(layout.links);
    for(TextLayout::Line layoutLine: layout.lines) {
        for(uint line: range(layoutLine.begin.line,layoutLine.end.line)) {
            const TextLayout::TextLine& textLine = layout.text[line];
            if(layoutLine.begin.column<textLine.size()) {
                TextLayout::Character first = line==layoutLine.begin.line ? textLine[layoutLine.begin.column] : textLine.first();
                TextLayout::Character last = line==layoutLine.end.line ? textLine[layoutLine.end.column] : textLine.last();
                lines << Line __( int2(first.pos+vec2(0,2)), int2(last.pos+vec2(last.font?last.font->advance(last.index),2:0)));
            }
        }
    }
}
int2 Text::sizeHint() {
    if(!textSize) layout();
    return max(minSize,textSize);
}
void Text::render(int2 position, int2 size) {
    if(!textSize) layout();
    int2 offset = position+max(int2(0),(size-textSize)/2);
    for(const TextLine& line: textLines) for(const Character& b: line) if(b.image) substract(offset+b.pos, b.image, 0xFF-opacity);
    for(const Line& l: lines) fill(offset+Rect(l.min-int2(0,1),l.max), black);
}

bool Text::mouseEvent(int2 position, int2 size, Event event, Button) {
    if(event!=Press) return false;
    position -= max(int2(0),(size-textSize)/2);
    if(!Rect(textSize).contains(position)) return false;
    for(uint line: range(textLines.size())) {
        if(position.y < (int)(line*this->size) || position.y > (int)(line+1)*this->size) continue;
        const TextLine& textLine = textLines[line];
        if(!textLine) goto break_;
        // Before first character
        const Character& first = textLine.first();
        if(position.x <= first.center) { cursor = Cursor __(line,0); goto break_; }
        // Between characters
        for(uint column: range(0,textLine.size()-1)) {
            const Character& prev = textLine[column];
            const Character& next = textLine[column+1];
            if(position.x >= prev.center && position.x <= next.center) { cursor = Cursor __(line,column+1); goto break_; }
        }
        // After last character
        const Character& last = textLine.last();
        if(position.x >= last.center) { cursor = Cursor __(line,textLine.size()); goto break_; }
    }
    if(textClicked) { textClicked(); return true; }
    return false;
    break_:;
    for(const Link& link: links) if(cursor>link.begin && link.end>cursor) { linkActivated(link.identifier); return true; }
    if(textClicked) { textClicked(); return true; }
    return false;
}

/// TextInput

bool TextInput::mouseEvent(int2 position, int2 size, Event event, Button button) {
    setCursor(position+Rect(size),::Cursor::Text);
    if(Text::mouseEvent(position,size,event,button)) return true;
    if(event!=Press) return false;
    focus=this;
    if(button==MiddleButton) {
        string selection=getSelection();
        //text.insert(cursor,move(selection)); cursor+=selection.size(); TODO
        text<<toUTF32(getSelection()); layout(); cursor=Cursor(textLines.size(),textLines.last().size());
        return true;
    }
    if(cursor.line!=last.line || cursor.column!=last.column) {
        last=cursor;
        return true;
    }
    return false;
}

bool TextInput::keyPress(Key key unused) {
    cursor.line=min(cursor.line,textLines.size()-1);
    const TextLine& textLine = textLines[cursor.line];

    if(key==UpArrow) {
        if(cursor.line>0) cursor.line--;
    } else if(key==DownArrow) {
         if(cursor.line<textLines.size()-1) cursor.line++;
    } else {
        cursor.column=min(cursor.column,textLine.size());

        /**/  if(key==LeftArrow) {
            if(cursor.column>0) cursor.column--;
            else if(cursor.line>0) cursor.line--, cursor.column=textLines[cursor.line].size();
        }
        else if(key==RightArrow) {
            if(cursor.column<textLine.size()) cursor.column++;
            else if(cursor.line<textLines.size()-1) cursor.line++, cursor.column=0;
        }
        else if(key==Home) cursor.column=0;
        else if(key==End) cursor.column=textLine.size();
        else if(key==Delete) {
            if(cursor.column<textLine.size() || cursor.line<textLines.size()-1) {
                editIndex=index(); text.removeAt(index()); textSize=0; if(textChanged) textChanged(toUTF8(text));
            }
        }
        else if(key==BackSpace) { //LeftArrow+Delete
            if(cursor.column>0) cursor.column--;
            else if(cursor.line>0) cursor.line--, cursor.column=textLines[cursor.line].size();
            else return false;
            if(index()<text.size()) {
                editIndex=index();  text.removeAt(index()); textSize=0; if(textChanged) textChanged(toUTF8(text));
            }
        }
        else if(key==Return) {
            if(textEntered) textEntered(toUTF8(text));
            else {
                editIndex=index()+1; text.insertAt(index(),'\n'); textSize=0; if(textChanged) textChanged(toUTF8(text));
            }
        }
        else {
            //prevent repeated whitespace
            if(" \t"_.contains(key)  &&
                    ((cursor.column<textLines[cursor.line].size() && " \t"_.contains(text[index()]))
                     || (index()>0 && " \t"_.contains(text[index()-1]))) ) return false;
            char c=0;
            if(key>=' ' && key<=0xFF) c=key; //TODO: UTF8 Compose
            else if(key>=KP_0 && key<=KP_9) c=key-KP_0+'0';
            else if(key==KP_Multiply) c='*'; else if(key==KP_Add) c='+'; else if(key==KP_Sub) c='-'; else if(key==KP_Divide) c='/';
            else return false;
            editIndex=index()+1; if(text) text.insertAt(index(), c); else text<<c; textSize=0; if(textChanged) textChanged(toUTF8(text));
        }
    }
    return true;
}

void TextInput::render(int2 position, int2 size) {
    Text::render(position, size);
    if(focus==this) {
        assert(cursor.line < textLines.size(), cursor.line, textLines.size());
        const TextLine& textLine = textLines[cursor.line];
        int x = 0;
        if(cursor.column<textLine.size()) x= textLine[cursor.column].pos.x;
        else if(textLine) x=textLine.last().pos.x+textLine.last().advance;
        int2 offset = position+max(int2(0),(size-textSize)/2);
        fill(offset+int2(x,cursor.line*this->size)+Rect(1,this->size), black);
    }
}

