#include "text.h"
#include "graphics.h"
#include "font.h"
#include "utf8.h"

map<int,Font> defaultSans;
map<int,Font> defaultBold;
map<int,Font> defaultItalic;
map<int,Font> defaultMono;

/// Layouts formatted text with wrapping, justification and links
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
    Text::Cursor current() { return Text::Cursor{lineNumber, column}; }

    uint lastIndex=-1;
    void nextLine(bool justify) {
        //justify
        float length=0; for(const Word& word: line) { if(word) { length+=word.last().pos.x+word.last().advance; } } //sum word length
        if(line && line.last()) length += line.last().last().width - line.last().last().advance; //for last word of line, use glyph bound instead of advance
        float space=0;
        if(justify && line.size>1) space = (wrap-length)/(line.size-1);
        if(space<=0 || space>=3*spaceAdvance) space = spaceAdvance; //compact

        //layout
        column=0; pen.x=0;
        lineNumber++; text << TextLine();
        for(uint i: range(line.size)) { Word& word=line[i];
            for(Character& c: word) text.last() << Character{c.font, pen+c.pos, c.index, 0, c.advance, lastIndex=c.editIndex};
            if(word) pen.x += word.last().pos.x+word.last().advance;
            if(i!=line.size-1) //editable justified space
                text.last() << Character{0,pen,0,0,spaceAdvance,lastIndex=lastIndex+1};
            pen.x += space;
        }
        lastIndex++;
        line.clear();
        pen.x=0; pen.y+=size;
    }

    TextLayout(const ref<uint>& text, int size, int wrap, Font* font=0):size(size),wrap(wrap) {
        static Folder dejavu( existsFolder("dejavu"_,fonts()) ?  "dejavu"_ : "truetype/ttf-dejavu/"_, fonts());
        if(!font) {
            if(!defaultSans.contains(size)) defaultSans.insert(size,Font(File("DejaVuSans.ttf"_,dejavu), size));
            font = &defaultSans.at(size);
        }
        uint16 spaceIndex = font->index(' ');
        spaceAdvance = font->advance(spaceIndex); assert(spaceAdvance);
        uint16 previous=spaceIndex;
        TextFormat format=Regular;
        Text::Link link;
        Text::Cursor underlineBegin;
        Word word;
        pen.y = font->ascender;
        for(uint i=0; i<text.size; i++) {
            uint c = text[i];
            if(c==' '||c=='\t'||c=='\n') { //next word/line
                column++;
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
                TextFormat newFormat = ::format(c);
                if(format&Underline && !(newFormat&Underline) && (current()>underlineBegin))
                    lines << Line{underlineBegin, current()};
                format=newFormat;
                if(format&Bold) {
                    if(!defaultBold.contains(size)) defaultBold.insert(size,Font(File("DejaVuSans-Bold.ttf"_,dejavu), size));
                    font = &defaultBold.at(size);
                } else if(format&Italic) {
                    if(!defaultItalic.contains(size)) defaultItalic.insert(size,Font(File("DejaVuSans-Oblique.ttf"_,dejavu), size));
                    font = &defaultItalic.at(size);
                } else {
                    if(!defaultSans.contains(size)) defaultSans.insert(size,Font(File("DejaVuSans.ttf"_,dejavu), size));
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
            if(image) { word << Character{font, vec2(pen.x,0), index, image.width, advance, i}; column++; }
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

Text::Text(const string& text, uint size, vec3 color, float alpha, uint wrap) : text(toUTF32(text)), size(size), color(color), alpha(alpha), wrap(wrap) {}
void Text::layout() {
    textSize=int2(0,size);
    TextLayout layout(text, size, wrap);

    textLines.clear(); textLines.reserve(layout.text.size);
    cursor=Cursor(0,0); uint currentIndex=0;
    for(const TextLayout::TextLine& line: layout.text) {
        TextLine textLine;
        for(const TextLayout::Character& o: line) {
            currentIndex = o.editIndex;
            if(currentIndex<=editIndex) { //restore cursor after relayout
                cursor = Cursor(textLines.size, textLine.size);
            }
            if(o.font) {
                const Glyph& glyph=o.font->glyph(o.index,o.pos.x);
                Character c{int2(o.pos)+glyph.offset, share(glyph.image), o.editIndex, int(o.pos.x+o.advance/2), (int)glyph.image.height, int(o.advance)};
                textSize=max(textSize,int2(c.pos)+c.image.size());
                textLine << move(c);
            } else { //format character
                textLine << Character{int2(o.pos),Image(),o.editIndex,int(o.pos.x+o.advance/2), this->size, int(o.advance)};
            }
        }
        currentIndex++;
        if(currentIndex<=editIndex) cursor = Cursor(textLines.size, textLine.size); //end of line
        textLines << move(textLine);
    }
    if(!text.size) { assert(editIndex==0); cursor = Cursor(0,0); }
    else if(currentIndex<=editIndex) { assert(textLines); cursor = Cursor(textLines.size-1, textLines.last().size); } //end of text
    links = move(layout.links);
    for(TextLayout::Line layoutLine: layout.lines) {
        for(uint line: range(layoutLine.begin.line, layoutLine.end.line+1)) {
            const TextLayout::TextLine& textLine = layout.text[line];
            if(layoutLine.begin.column<textLine.size) {
                TextLayout::Character first = (line==layoutLine.begin.line) ? textLine[layoutLine.begin.column] : textLine.first();
                TextLayout::Character last = (line==layoutLine.end.line && layoutLine.end.column<textLine.size) ? textLine[layoutLine.end.column] : textLine.last();
                assert(first.pos.y == last.pos.y);
                lines << Line{ int2(first.pos+vec2(0,1)), int2(last.pos+vec2(last.font?last.font->advance(last.index):0,2))};
            }
        }
    }
}
int2 Text::sizeHint() {
    if(!textSize) layout();
    return max(minSize,textSize);
}
void Text::render(const Image& target) {
    if(!textSize) layout();
    int2 offset = max(int2(0),(target.size()-textSize)/2);
    for(const TextLine& line: textLines) for(const Character& b: line) if(b.image) blit(target, offset+b.pos, b.image, color);
    for(const Line& l: lines) fill(target, offset+Rect(l.min,l.max), black);
}

bool Text::mouseEvent(int2 position, int2 size, Event event, Button button) {
    if(event==Release || (event==Motion && !button)) return false;
    position -= max(int2(0),(size-textSize)/2);
    if(!Rect(textSize).contains(position)) return false;
    for(uint line: range(textLines.size)) {
        if(position.y < (int)(line*this->size) || position.y > (int)(line+1)*this->size) continue;
        const TextLine& textLine = textLines[line];
        if(!textLine) goto break_;
        // Before first character
        const Character& first = textLine.first();
        if(position.x <= first.center) { cursor = Cursor(line,0); goto break_; }
        // Between characters
        for(uint column: range(0,textLine.size-1)) {
            const Character& prev = textLine[column];
            const Character& next = textLine[column+1];
            if(position.x >= prev.center && position.x <= next.center) { cursor = Cursor(line,column+1); goto break_; }
        }
        // After last character
        const Character& last = textLine.last();
        if(position.x >= last.center) { cursor = Cursor(line,textLine.size); goto break_; }
    }
    if(event == Press && textClicked) { textClicked(); return true; }
    break_:;
    if(event == Press) for(const Link& link: links) if(link.begin<cursor && cursor<link.end) { linkActivated(link.identifier); return true; }
    if(event == Press && textClicked) { textClicked(); return true; }
    return false;
}

uint Text::index() {
    if(!textLines) return 0;
    if(cursor.line==textLines.size) return textLines.last().last().editIndex;
    assert(cursor.line<textLines.size,cursor.line,textLines.size);
    assert(cursor.column<=textLines[cursor.line].size, cursor.column, textLines[cursor.line].size);
    if(cursor.column<textLines[cursor.line].size) {
        uint index = textLines[cursor.line][cursor.column].editIndex;
        assert(index<text.size);
        return index;
    }
    uint index = 1; // ' ', '\t' or '\n' immediately after last character
    uint line=cursor.line;
    while(line>0 && !textLines[line]) line--, index++; //count \n (not included as characters)
    if(textLines[line]) index += textLines[line].last().editIndex;
    return index;
}

/// TextInput

bool TextInput::mouseEvent(int2 position, int2 size, Event event, Button button) {
    setCursor(position+Rect(size),::Cursor::Text);
    if(event==Press) setFocus(this);
    if(event==Press && button==MiddleButton) {
        Text::mouseEvent(position,size,event,button);
        array<uint> selection = toUTF32(getSelection());
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
        array<uint> selection = toUTF32(getSelection(true));
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
        else if(key==BackSpace) { //LeftArrow+Delete
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
            char c=0;
            if(key>=' ' && key<=0xFF) c=key; //TODO: UTF8 Compose
            else if(key>=KP_0 && key<=KP_9) c=key-KP_0+'0';
            else if(key==KP_Multiply) c='*'; else if(key==KP_Add) c='+'; else if(key==KP_Sub) c='-'; else if(key==KP_Divide) c='/';
            else return false;
            editIndex=index()+1; if(text) text.insertAt(index(), c); else text<<c, editIndex=1; layout(); if(textChanged) textChanged(toUTF8(text));
        }
    }
    if(!(modifiers&Shift)) selectionStart=cursor;
    return true;
}

void TextInput::render(const Image& target) {
    Text::render(target);
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
