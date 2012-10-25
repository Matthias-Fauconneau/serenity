#include "text.h"
#include "display.h"
#include "font.h"
#include "utf8.h"

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
    struct Character { Font* font; vec2 pos; uint index; uint width; float advance; };
    typedef array<Character> Word;
    array<Word> line;
    array<Character> text;
    struct Line { uint begin,end; };
    array<Line> lines;
    array<Text::Link> links;

    void nextLine(bool justify) {
        if(!line) { pen.y+=size; return; }
        //justify
        float length=0; for(const Word& word: line) length+=word.last().pos.x+word.last().advance; //sum word length
        length += line.last().last().width - line.last().last().advance; //for last word of line, use glyph bound instead of advance
        float space=0;
        if(justify && line.size()>1) space = (wrap-length)/(line.size()-1);
        if(space<=0 || space>=2*spaceAdvance) space = spaceAdvance; //compact

        //layout
        pen.x=0;
        for(Word& word: line) {
            assert(word);
            for(Character& c: word) text << Character __(c.font, pen+c.pos, c.index, 0, c.advance);
            pen.x += word.last().pos.x+word.last().advance+space;
        }
        line.clear();
        pen.x=0; pen.y+=size;
    }

    TextLayout(const ref<byte>& text, int size, int wrap, Font* font=0):size(size),wrap(wrap) {
        if(!font) {
            if(!defaultSans.contains(size)) defaultSans.insert(size,Font(File("dejavu/DejaVuSans.ttf"_,fonts()), size));
            font = &defaultSans.at(size);
        }
        uint16 spaceIndex = font->index(' ');
        spaceAdvance = font->advance(spaceIndex); assert(spaceAdvance);
        uint16 previous=spaceIndex;
        Format format=Regular;
        Text::Link link;
        uint underlineBegin=0;
        uint glyphCount=0;
        Word word;
        pen.y = font->ascender;
        for(utf8_iterator it=text.begin();it!=(utf8_iterator)text.end();++it) {
            uint c = *it;
            if(c==' '||c=='\t'||c=='\n') {//next word/line
                previous = spaceIndex;
                if(!word) { if(c=='\n') nextLine(false); continue; }
                float length=0; for(const Word& word: line) length+=word.last().pos.x+word.last().advance+spaceAdvance;
                length += word.last().pos.x+word.last().width; //last word
                if(wrap && length>=wrap) nextLine(true); //doesn't fit
                line << move(word); //add to current line (or first of new line)
                pen.x=0;
                if(c=='\n') nextLine(false);
                continue;
            }
            if(c<0x20) { //00-1F format control flags (bold,italic,underline,strike,link)
                if(format&Link) {
                    link.end=glyphCount;
                    links << Text::Link __(link.begin,link.end,move(link.identifier));
                }
                Format newFormat = ::format(c);
                if(format&Underline && !(newFormat&Underline) && glyphCount>underlineBegin) lines << Line __(underlineBegin,glyphCount);
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
                if(format&Underline) underlineBegin=glyphCount;
                if(format&Link) {
                    for(;;) {
                        ++it;
                        assert(it!=(utf8_iterator)text.end(),text);
                        uint c = *it;
                        if(c == ' ') break;
                        link.identifier << utf8(c);
                    }
                    link.begin=glyphCount;
                }
                continue;
            }
            uint16 index = font->index(c);
            if(previous!=spaceIndex) pen.x += font->kerning(previous,index);
            previous = index;
            float advance = font->advance(index);
            const Image& image = font->glyph(index).image;
            if(image) { word << Character __(font, vec2(pen.x,0), index, image.width, advance); glyphCount++; }
            pen.x += advance;
        }
        if(!text || text[text.size-1]!='\n') {
            if(word) {
                float length=0; for(const Word& word: line) length+=word.last().pos.x+word.last().advance+spaceAdvance;
                length += word.last().pos.x+word.last().width; //last word
                if(wrap && length>=wrap) nextLine(true); //doesn't fit
                line << move(word); //add to current line (or first of new line)
                pen.x=0;
            }
            nextLine(false);
        }
    }
};

Text::Text(string&& text, int size, uint8 opacity, uint wrap) : text(move(text)), size(size), opacity(opacity), wrap(wrap) {}
void Text::layout() {
    textSize=int2(0,size);
    TextLayout layout(text, size, wrap);

    characters.clear(); characters.reserve(layout.text.size());
    for(TextLayout::Character o: layout.text) {
        const Glyph& glyph=o.font->glyph(o.index,o.pos.x);
        Character c __(int2(o.pos)+glyph.offset, share(glyph.image));
        textSize=max(textSize,int2(c.pos)+c.image.size());
        characters << move(c);
    }

    links = move(layout.links);
    for(TextLayout::Line l: layout.lines) {
        Line line;
        TextLayout::Character c = layout.text[l.begin];
        line.min = int2(c.pos) + int2(0,2);
        for(uint i: range(l.begin,l.end)) {
            TextLayout::Character c = layout.text[i];
            int2 p = int2(c.pos) + int2(0,2);
            if(p.y!=line.min.y) lines<< move(line), line.min=p; else line.max=p+int2(c.font->advance(c.index),0);
        }
        if(line.max != line.min) lines<< move(line);
    }
}
int2 Text::sizeHint() {
    if(!textSize) layout();
    return max(minSize,textSize);
}
void Text::render(int2 position, int2 size) {
    if(!textSize) layout();
    int2 offset = position+max(int2(0),(size-textSize)/2);
    for(const Character& b: characters) substract(offset+b.pos, b.image, 0xFF-opacity);
    for(const Line& l: lines) fill(offset+Rect(l.min-int2(0,1),l.max), black);
}

bool Text::mouseEvent(int2 position, int2 size, Event event, Button) {
    if(event!=Press) return false;
    position -= max(int2(0),(size-textSize)/2);
    if(!Rect(textSize).contains(position)) return false;
    for(uint i: range(characters.size())) { const Character& b=characters[i];
        if((b.pos+Rect(b.image.size())).contains(position)) {
            for(const Link& link: links) if(i>=link.begin&&i<=link.end) { linkActivated(link.identifier); return true; }
        }
    }
    if(textClicked) { textClicked(); return true; }
    return false;
}

/// TextInput

bool TextInput::mouseEvent(int2 position, int2 size, Event event, Button button) {
    if(event!=Press) return false;
    focus=this;
    position -= max(int2(0,0),(size-textSize)/2);
    for(cursor=0;cursor<characters.size() && position.x>characters[cursor].pos.x+(int)characters[cursor].image.width/2;cursor++) {}
    if(button==MiddleButton) { string selection=getSelection(); cursor+=selection.size(); text<<move(selection); textSize=0; }
    return true;
}

bool TextInput::keyPress(Key key) {
    if(cursor>text.size()) cursor=text.size();
    /**/  if(key==LeftArrow && cursor>0) cursor--;
    else if(key==RightArrow && cursor<text.size()) cursor++;
    else if(key==Home) cursor=0;
    else if(key==End) cursor=text.size();
    else if(key==Delete && cursor<text.size()) text.removeAt(cursor);
    else if(key==BackSpace && cursor>0) text.removeAt(--cursor);
    else if(key==Return) textEntered(text);
    else {
        char c=0;
        if(key>=' ' && key<=0xFF) c=key; //TODO: UTF8 Compose
        else if(key>=KP_0 && key<=KP_9) c=key-KP_0+'0';
        else if(key==KP_Multiply) c='*'; else if(key==KP_Add) c='+'; else if(key==KP_Subtract) c='-'; else if(key==KP_Divide) c='/';
        else return false;
        text.insertAt(cursor++, c);
    }
    textSize=0; textChanged(text); return true;
}

void TextInput::render(int2 position, int2 size) {
    Text::render(position, size);
    if(focus==this) {
        if(cursor>text.size()) cursor=text.size();
        int x = cursor < characters.size()? characters[cursor].pos.x : (cursor>0 && characters) ? characters.last().pos.x+characters.last().image.width : 0;
        fill(position+max(int2(0,0), (size-textSize)/2)+Rect(int2(x,0), int2(x+1,textSize.y)), black);
    }
}

