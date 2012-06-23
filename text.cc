#include "text.h"
#include "display.h"
#include "font.h"

#include "array.cc"
template struct array<Text>;
template struct array<Text::Blit>;
Array(Text::Line)
template struct array<Text::Link>;

Widget* focus;

// All coordinates are .8 fixed point
struct TextLayout {
    int size;
    int wrap;
    Font* font = 0;
    int2 pen;
    struct Character { int code; int2 pos; const Pixmap glyph; };
    typedef array<Character> Word;
    array<Word> line;
    Word word;
    array<Character> text;
    array<Text::Link> links;
    struct Line { int begin,end; };
    array<Line> lines;

    void nextLine(bool justify) {
        if(!line) { pen.y+=size; return; }
        //justify
        float length=0; for(const Word& word: line) length+=word.last().pos.x+word.last().glyph.advance.x; //sum word length
        length += line.last().last().glyph.pixmap.width - line.last().last().glyph.advance.x; //for last word of line, use glyph bound instead of advance
        float space=0;
        if(justify && line.size()>1) space = (wrap-length)/(line.size()-1);
        if(space<=0||space>64) space = font->glyph(size,' ').advance.x; //compact

        //layout
        pen.x=0;
        for(const Word& word: line) {
            assert(word);
            for(const Character& c: word) text << Character{c.code,pen+c.pos,c.glyph};
            pen.x += word.last().pos.x+word.last().glyph.advance.x+space;
        }
        line.clear();
        pen.x=0; pen.y+=size;
    }

    TextLayout(int size, int wrap, const string& s):size(size<<8),wrap(wrap<<8) {
        uint previous=' ';
        Format format=Format::Regular;
        Text::Link link;
        uint underlineBegin=0;
        uint glyphCount=0;
        for(utf8_iterator it=s.begin();it!=s.end();++it) {
            uint c = *it;
            if(c==' '||c=='\t'||c=='\n') {//next word/line
                if(c==' ') previous = c;
                if(!word) { if(c=='\n') nextLine(false); continue; }
                float length=0; for(const Word& word: line) length+=word.last().pos.x+word.last().glyph.advance.x+font->glyph(size,' ').advance.x;
                length += word.last().pos.x+word.last().glyph.pixmap.width; //last word
                if(wrap && length>=wrap) nextLine(true); //doesn't fit
                line << move(word); //add to current line (or first of new line)
                pen.x=0;
                if(c=='\n') nextLine(false);
                continue;
            }
            if(c<0x20) { //00-1F format control flags (bold,italic,underline,strike,link)
                assert(c==Format::Regular||c==Format::Bold||c==Format::Italic||c==(Format::Link|Format::Underline));
                if(format&Format::Link) {
                    link.end=glyphCount;
                    links << Text::Link{link.begin,link.end,move(link.identifier)};
                }
                auto newFormat = ::format(c);
                if(format&Underline && !(newFormat&Underline) && glyphCount>underlineBegin) lines << Line{underlineBegin,glyphCount};
                format=newFormat;
                static Font defaultSans("truetype/ttf-dejavu/DejaVuSans.ttf"_);
                //static Font defaultBold("truetype/ttf-dejavu/DejaVuSans-Bold.ttf"_);
                //static Font defaultItalic("truetype/ttf-dejavu/DejaVuSans-Oblique.ttf"_);
                //static Font defaultBoldItalic("truetype/ttf-dejavu/DejaVuSans-BoldOblique.ttf"_);*/
                /*Font* lookup[] = {&defaultSans,&defaultBold,&defaultItalic,&defaultBoldItalic};
                font=lookup[format&(Format::Bold|Format::Italic)];*/
                font=&defaultSans;
                if(format&Underline) underlineBegin=glyphCount;
                if(format&Link) {
                    for(;;) {
                        ++it;
                        assert(it!=s.end(),s);
                        uint c = *it;
                        if(c == ' ') break;
                        link.identifier << utf8(c);
                    }
                    link.begin=glyphCount;
                }
                continue;
            }
            const Glyph& glyph = font->glyph(size,c);
            pen.x += font->kerning(previous,c);
            previous = c;
            if(glyph.pixmap) { word << i(Character{c, int2(pen.x,0)+glyph.offset, glyph }); glyphCount++; }
            pen.x += glyph.advance.x;
        }
    }
    int2 textSize() {
       int2 max;
       for(Character c: text) max=::max(max,int2(c.pos)+c.glyph.pixmap.size());
       max.y=::max(max.y, size);
       return max;
    }
};

Array(TextLayout::Character)
Array(TextLayout::Word)
Array(TextLayout::Line)

Text::Text(string&& text, int size, ubyte opacity, int wrap) : text(move(text)), size(size), opacity(opacity), wrap(wrap) {}
void Text::update(int wrap) {
    lines.clear();
    blits.clear();
    if(text.last()!='\n') text << '\n';
    TextLayout layout(size, wrap>=0 ? wrap : Widget::size.x+wrap, text);
    for(const TextLayout::Character& c: layout.text) blits << i(Blit{int2(round(c.pos.x),round(c.pos.y)),c.glyph.pixmap});
    for(const TextLayout::Line& l: layout.lines) {
        Line line;
        const auto& c = layout.text[l.begin];
        line.min=int2(c.pos-c.glyph.offset);
        for(int i=l.begin;i<l.end;i++) {
            const auto& c = layout.text[i];
            int2 p = int2(c.pos) - int2(0,c.glyph.offset.y);
            if(p.y!=line.min.y) lines<<move(line), line.min=p; else line.max=p+int2(c.glyph.advance.x,0);
        }
        if(line.max!=line.min) lines<<move(line);
    }
    links = move(layout.links);
    textSize = layout.textSize();
}
int2 Text::sizeHint() {
    if(!textSize) update(wrap>=0 ? wrap : screen.y);
    return wrap?int2(-textSize.x,textSize.y):textSize;
}

void Text::render(int2 parent) {
    if(!textSize) update(wrap>=0 ? wrap : screen.y);
    int2 offset = parent+position+max(int2(0,0),(Widget::size-textSize)/2);
    for(const Blit& b: blits) blit(offset+b.pos, b.pixmap, MultiplyAlpha, opacity);
    for(const Line& l: lines) fill(offset+Rect(l.min+int2(0,1),l.max+int2(0,2)), black);
}

bool Text::mouseEvent(int2 position, Event event, Button) {
    if(event!=Press) return false;
    position -= max(int2(0,0),(Widget::size-textSize)/2);
    for(uint i=0;i<blits.size();i++) { const Blit& b=blits[i];
        if(position>=b.pos && position<=b.pos+b.pixmap.size()) {
            for(const Link& link: links) if(i>=link.begin&&i<=link.end) { linkActivated.emit(link.identifier); return true; }
        }
    }
    if(textClicked.slots) { textClicked.emit(); return true; }
    return false;
}

/// TextInput

bool TextInput::mouseEvent(int2 position, Event event, Button) {
    if(event!=Press) return false;
    focus=this;
    int x = position.x-(this->position.x+(Widget::size.x-textSize.x)/2);
    for(cursor=0;cursor<blits.size() && x>blits[cursor].pos.x+(int)blits[cursor].pixmap.width/2;cursor++) {}
    //if(button==MiddleButton) { string selection=getSelection(); cursor+=selection.size(); text<<move(selection); update(); }
    return true;
}

bool TextInput::keyPress(Key key) {
    if(cursor>text.size()) cursor=text.size();
    /**/ if(key==Left && cursor>0) cursor--;
    else if(key==Right && cursor<text.size()) cursor++;
    else if(key==Home) cursor=0;
    else if(key==End) cursor=text.size();
    else if(key==Delete && cursor<text.size()) { text.removeAt(cursor); update(); }
    else if(key==BackSpace && cursor>0) { text.removeAt(--cursor); update(); }
    else if(!(key&0xff00)) { insertAt(text, cursor++, (byte)key); update(); } //TODO: shift
    else return false;
    return true;
}

void TextInput::render(int2 parent) {
    Text::render(parent);
    if(focus==this) {
        if(cursor>text.size()) cursor=text.size();
        int x = cursor < blits.size()? blits[cursor].pos.x : cursor>0 ? blits.last().pos.x+blits.last().pixmap.width : 0;
        fill(parent+position+max(int2(0,0),(Widget::size-textSize)/2)+Rect(int2(x,0), int2(x+1,Widget::size.y)),gray(0));
    }
}

