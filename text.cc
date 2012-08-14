#include "text.h"
#include "display.h"
#include "font.h"
#include "utf8.h"

struct TextLayout {
    int size;
    int wrap;
    Font* font = 0;
    int2 pen; //.4
    struct Character { int2 pos;/*.4*/ Glyph glyph; };
    typedef array<Character> Word;
    array<Word> line;
    Word word;
    array<Character> text;
    array<Text::Link> links;
    struct Line { uint begin,end; };
    array<Line> lines;

    void nextLine(bool justify) {
        if(!line) { pen.y+=size<<4; return; }
        //justify
        int length=0; for(const Word& word: line) length+=word.last().pos.x+word.last().glyph.advance; //sum word length
        length += line.last().last().glyph.image.width - line.last().last().glyph.advance; //for last word of line, use glyph bound instead of advance
        int space=0;
        if(justify && line.size()>1) space = ((wrap<<4)-length)/(line.size()-1);
        if(space<=0||space>64<<4) space = font->glyph(' ').advance; //compact

        //layout
        pen.x=0;
        for(Word& word: line) {
            assert(word);
            for(Character& c: word) text << i(Character{pen+c.pos, move(c.glyph)});
            pen.x += word.last().pos.x+word.last().glyph.advance+space;
        }
        line.clear();
        pen.x=0; pen.y+=size<<4;
    }

    TextLayout(int size, int wrap, const ref<byte>& text):size(size),wrap(wrap) {
        static map<int,Font> defaultSans;
        if(!defaultSans.contains(size)) defaultSans.insert(size,Font("dejavu/DejaVuSans.ttf"_, size));
        font=&defaultSans.at(size);
        uint previous=' ';
        Format format=Format::Regular;
        Text::Link link;
        uint underlineBegin=0;
        uint glyphCount=0;
        for(utf8_iterator it=text.begin();it!=text.end();++it) {
            uint c = *it;
            if(c==' '||c=='\t'||c=='\n') {//next word/line
                if(c==' ') previous = c;
                if(!word) { if(c=='\n') nextLine(false); continue; }
                int length=0; for(const Word& word: line) length+=word.last().pos.x+word.last().glyph.advance+font->glyph(' ').advance;
                length += word.last().pos.x+(word.last().glyph.image.width<<4); //last word
                if(wrap && length>=(wrap<<4)) nextLine(true); //doesn't fit
                line << move(word); //add to current line (or first of new line)
                pen.x=0;
                if(c=='\n') nextLine(false);
                continue;
            }
            if(c<0x20) { //00-1F format control flags (bold,italic,underline,strike,link)
                if(format&Format::Link) {
                    link.end=glyphCount;
                    links << Text::Link i({link.begin,link.end,move(link.identifier)});
                }
                Format newFormat = ::format(c);
                if(format&Underline && !(newFormat&Underline) && glyphCount>underlineBegin) lines << Line i({underlineBegin,glyphCount});
                format=newFormat;
                if(format&Format::Bold) {
                    static map<int,Font> defaultBold;
                    if(!defaultBold.contains(size)) defaultBold.insert(size,Font("dejavu/DejaVuSans-Bold.ttf"_, size));
                    font = &defaultBold.at(size);
                } else if(format&Format::Italic) {
                    static map<int,Font> defaultItalic;
                    if(!defaultItalic.contains(size)) defaultItalic.insert(size,Font("dejavu/DejaVuSans-Oblique.ttf"_, size));
                    font = &defaultItalic.at(size);
                } else {
                    if(!defaultSans.contains(size)) defaultSans.insert(size,Font("dejavu/DejaVuSans.ttf"_, size));
                    font = &defaultSans.at(size);
                }
                if(format&Underline) underlineBegin=glyphCount;
                if(format&Link) {
                    for(;;) {
                        ++it;
                        assert(it!=text.end(),text);
                        uint c = *it;
                        if(c == ' ') break;
                        link.identifier << utf8(c);
                    }
                    link.begin=glyphCount;
                }
                continue;
            }
            Glyph glyph = font->glyph(c);
            pen.x += font->kerning(previous,c);
            previous = c;
            if(glyph.image) word << i(Character{int2(pen.x,0)+glyph.offset, move(glyph) }); glyphCount++;
            pen.x += glyph.advance;
        }
        if(!text || text[text.size-1]!='\n') {
            if(word) {
                int length=0; for(const Word& word: line) length+=word.last().pos.x+word.last().glyph.advance+font->glyph(' ').advance;
                length += word.last().pos.x+(word.last().glyph.image.width<<4); //last word
                if(wrap && length>=(wrap<<4)) nextLine(true); //doesn't fit
                line << move(word); //add to current line (or first of new line)
                pen.x=0;
            }
            nextLine(false);
        }
    }
};

Text::Text(string&& text, int size, ubyte opacity, int wrap) : text(move(text)), size(size), opacity(opacity), wrap(wrap), textSize{0,0} {}
void Text::update(int wrap) {
    lines.clear();
    blits.clear();
    TextLayout layout(size, wrap>=0 ? wrap : Widget::size.x+wrap, text);
    for(const TextLayout::Character& c: layout.text) blits << i(Blit{int2((c.pos.x+8)>>4,(c.pos.y+8)>>4),share(c.glyph.image)});
    for(const TextLayout::Line& l: layout.lines) {
        Line line;
        const TextLayout::Character& c = layout.text[l.begin];
        line.min = c.pos-c.glyph.offset;
        for(uint i=l.begin;i<l.end;i++) {
            const TextLayout::Character& c = layout.text[i];
            int2 p = int2(c.pos) - int2(0,c.glyph.offset.y);
            if(p.y!=line.min.y) lines<< move(line), line.min=p; else line.max=p+int2(c.glyph.advance,0);
        }
        if(line.max != line.min) lines<< move(line);
    }
    links = move(layout.links);
    textSize=int2(0,0);
    for(const Blit& c: blits) textSize=max(textSize,int2(c.pos)+c.image.size());
    textSize.y=max(textSize.y, size+4);
}
int2 Text::sizeHint() {
    if(!textSize) update(wrap);
    return wrap?int2(-textSize.x,textSize.y):textSize;
}

void Text::render(int2 parent) {
    if(!textSize) update(wrap);
#if ACCURATE_BACKGROUND_FILL
    array<Rect> rects; rects<< parent+position+Rect(Widget::size);
    int2 offset = parent+position+max(int2(0,0),(Widget::size-textSize)/2);
    for(const Blit& b: blits) {
        Rect rect = offset+b.pos+Rect(b.image.size());
        blit(offset+b.pos, b.image);
        remove(rects, rect);
    }
    for(const Line& l: lines) {
        Rect rect = offset+Rect(l.min+int2(0,1),l.max+int2(0,2));
        fill(rect, black);
        remove(rects, rect);
    }
    for(Rect rect: rects) fill(rect);
#else //accurate fill optimization would probably reduce performance for text rendering
    fill(parent+position+Rect(Widget::size));
    int2 offset = parent+position+max(int2(0,0),(Widget::size-textSize)/2);
    for(const Blit& b: blits) blit(offset+b.pos, b.image);
    for(const Line& l: lines) fill(offset+Rect(l.min+int2(0,1),l.max+int2(0,2)), black);
#endif
}

bool Text::mouseEvent(int2 position, Event event, Key) {
    if(event!=ButtonPress) return false;
    position -= max(int2(0,0),(Widget::size-textSize)/2);
    for(uint i=0;i<blits.size();i++) { const Blit& b=blits[i];
        if(position>=b.pos && position<=b.pos+b.image.size()) {
            for(const Link& link: links) if(i>=link.begin&&i<=link.end) { linkActivated(link.identifier); return true; }
        }
    }
    if(textClicked.slots) { textClicked(); return true; }
    return false;
}

/// TextInput

bool TextInput::mouseEvent(int2 position, Event event, Key) {
    if(event!=ButtonPress) return false;
    focus=this;
    int x = position.x-(this->position.x+(Widget::size.x-textSize.x)/2);
    for(cursor=0;cursor<blits.size() && x>blits[cursor].pos.x+(int)blits[cursor].image.width/2;cursor++) {}
    //if(button==MiddleKey) { string selection=getSelection(); cursor+=selection.size(); text<<move(selection); update(); }
    return true;
}

bool TextInput::keyPress(Key key) {
    if(cursor>text.size()) cursor=text.size();
    /***/ if(key==Left && cursor>0) cursor--;
    else if(key==Right && cursor<text.size()) cursor++;
    else if(key==Home) cursor=0;
    else if(key==End) cursor=text.size();
    else if(key==Delete && cursor<text.size()) { text.removeAt(cursor); update(); }
    else if(key==BackSpace && cursor>0) { text.removeAt(--cursor); update(); }
    else if(key>=' ' && key<=0xFF) { //TODO: UTF8
        text.insertAt(cursor++, (byte)key); update();
    } else return false;
    return true;
}

void TextInput::render(int2 parent) {
    Text::render(parent);
    if(focus==this) {
        if(cursor>text.size()) cursor=text.size();
        int x = cursor < blits.size()? blits[cursor].pos.x : cursor>0 ? blits.last().pos.x+blits.last().image.width : 0;
        fill(parent+position+max(int2(0,0), (Widget::size-textSize)/2)+Rect(int2(x,0), int2(x+1,Widget::size.y)), black);
    }
}

