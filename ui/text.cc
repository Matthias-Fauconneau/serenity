#include "text.h"
#include "graphics.h"
#include "font.h"
#include "utf8.h"

struct NameSize { String name; float size; };
bool operator ==(const NameSize& a, const NameSize& b) { return a.name == b.name && a.size == b.size; }
string str(const NameSize& x) { return str(x.name, x.size); }

/// Returns a font, loading from disk and caching as needed
Font* getFont(string fontName, float size, ref<string> fontTypes, bool hint) {
    String key = fontName+fontTypes[0]+(hint?"H"_:""_);
    static map<NameSize,unique<Font>> fonts; // Font cache
    unique<Font>* font = fonts.find(NameSize{copy(key), size});
    return font ? font->pointer
                : fonts.insert(NameSize{copy(key),size},unique<Font>(Map(findFont(fontName, fontTypes), fontFolder()), size, hint)).pointer;
}

/// Layouts formatted text with wrapping, justification and links
struct TextLayout {
    struct Glyph : Font::Metrics, ::Glyph {
        //uint editIndex;
        Glyph(const Font::Metrics& metrics, const ::Glyph& glyph) : Font::Metrics(metrics), ::Glyph(glyph) {}
    };
    float width(const ref<Glyph>& word) { float max=0; for(const Glyph& c : word) max=::max(max, c.origin.x + c.width); return max; }
    float advance(const ref<Glyph>& word) { float max=0; for(const Glyph& c : word) max=::max(max, c.origin.x + c.advance); return max; }

    // Parameters
    float size;
    float wrap;
    float interline;
    float spaceAdvance;

    // Variables
    float penY=0;
    array<array<Glyph>> words;
    //vec2 bbMin = inf;
    vec2 bbMax = -inf;
    /*uint lastIndex=-1;
    uint lineNumber=0, column=0;
    Text::Cursor current() { return Text::Cursor{lineNumber, column}; }*/

    // Outputs
    array<array<::Glyph>> glyphs;
    /*struct Line { Text::Cursor begin,end; };
    array<Line> lines;
    array<Text::Link> links;*/

    void nextLine(bool justify) {
        if(words) {
            // Justifies
            float length=0; for(const ref<Glyph>& word: words) length += advance(word); // Sums word lengths
            // For last word of line, use last glyph width instead of advance
            if(words.last()) length += -advance(words.last()) + width(words.last());
            float space=0;
            if(justify && words.size>1) space = (wrap-length)/(words.size-1);
            else space = spaceAdvance;

            // Layouts
            /*column=0;*/ float penX=0; // Line pen
            /*lineNumber++;*/
            auto& line = glyphs.append();
            for(const ref<Glyph>& word: words) {
                for(Glyph glyph: word) {
                    glyph.origin += vec2(penX,penY);
                    line << glyph; //lastIndex=glyph.editIndex
                    //bbMin=min(bbMin, glyph.origin-glyph.bearing);
                    bbMax=max(bbMax, glyph.origin-glyph.bearing+glyph.size);
                }
                penX += advance(word);
                penX += space;
            }
            //lastIndex++;
            words.clear();
        }
        penY += interline*size;
    }

    TextLayout(const ref<uint>& text, float size, float wrap, string fontName, bool hint, float interline, bool justify=false)
        : size(size), wrap(wrap), interline(interline) {
        assert_(wrap >= 0, wrap);
        Font* font = getFont(fontName, size, {""_,"R"_,"Regular"_}, hint);
        assert_(font, fontName, size);
        uint16 spaceIndex = font->index(' ');
        spaceAdvance = font->metrics(spaceIndex).advance; assert(spaceAdvance);
        uint16 previous=spaceIndex;
        bool bold=false, italic=false, superscript=false;
        int subscript = 0;
        float fontSize = size;
        array<float> yOffsetStack;
        float yOffset = 0;
        //Text::Cursor underlineBegin;
        array<Glyph> word;
        float penX = 0 , subscriptPen = 0; // Word pen
        int previousRightDelta = 0;
        penY = interline*font->ascender;
        uint i=0;
#if 1
        for(; i<text.size; i++) {
            uint c = text[i];
            /***/ if(c==' ') penX += spaceAdvance;
            else if(c=='\t') penX += 4*spaceAdvance; //FIXME: align
            else break;
        }
#endif
        for(; i<text.size; i++) {
            uint c = text[i];

            const float subscriptScale = 2./3, superscriptScale = subscriptScale;
            if(c<=Superscript) { //00-1F format control flags (bold,italic,underline,strike,link)
                if(c==' '||c=='\t'||c=='\n') continue;
                /**/ if(c==Bold) bold=!bold;
                else if(c==Italic) italic=!italic;
                else if(c==Superscript)  superscript=!superscript;
                else if(c==SubscriptStart) {
                    subscript++;
                    if(subscript==1) subscriptPen=penX;
                    yOffsetStack << yOffset;
                }
                else if(c==SubscriptEnd) {
                    subscript--;
                    assert_(subscript>=0);
                    yOffset = yOffsetStack.pop();
                }
                //else if(format==Underline) { if(underline && current()>underlineBegin) lines << Line{underlineBegin, current()}; }
                else error(c);
                fontSize = size;
                if(subscript) fontSize *= pow(subscriptScale, subscript);
                if(superscript) fontSize *= superscriptScale;
                if(bold && italic) font = getFont(fontName, fontSize, {"Bold Italic"_,"RBI"_,"BoldItalic"_}, hint);
                else if(bold) font = getFont(fontName, fontSize, {"Bold"_,"RB"_}, hint);
                else if(italic) font = getFont(fontName, fontSize, {"Italic"_,"I"_,"Oblique"_}, hint);
                else font = getFont(fontName, fontSize, {""_,"R"_,"Regular"_}, hint);
                assert_(font, fontName, bold, italic, subscript, superscript);
                //if(format&Underline) underlineBegin=current();
                if(c==SubscriptStart) yOffset += font->ascender/2;
                if(c==Superscript && superscript) yOffset -= fontSize/2;
                if(c==Superscript && !superscript) yOffset = 0;
                continue;
            }

            if(!subscript && !superscript) penX = max(subscriptPen, penX);

            if((c==' '||c=='\t'||c=='\n') && !subscript) { // Next word/line
                //column++;
                previous = spaceIndex;
                if(word) {
                    if(words) {
                        float length = 0;
                        for(const ref<Glyph>& word: words) length += advance(word) + spaceAdvance;
                        length += width(word); // Next word
                        if(wrap && length > wrap && words) nextLine(justify); // Would not fit
                    }
                    words << move(word); penX = 0; subscriptPen=0; // Add to current line (might be first of a new line)
                }
                if(c=='\t') penX += 4*spaceAdvance; //FIXME: align
                if(c=='\n') nextLine(false);
                continue;
            }

            uint16 index = font->index(c);

            float& pen = subscript ? subscriptPen : penX;

            if(previous != spaceIndex) pen += font->kerning(previous, index);
            previous = index;

            Font::Metrics metrics = font->metrics(index);

            /***/ if(previousRightDelta - metrics.leftDelta >= 32) pen--;
            else if(previousRightDelta - metrics.leftDelta < -32 ) pen++;
            previousRightDelta = metrics.rightDelta;

            if(c != ' ' && c != 0xA0) {
                int yGlyphOffset = 0;
                if(c==toUCS4("⌊"_)[0] || c==toUCS4("⌋"_)[0]) yGlyphOffset += fontSize/3; // Fixes too high floor signs from FreeSerif
                assert_(metrics.size, hex(c));
                word << Glyph(metrics,::Glyph{vec2(pen, yOffset+yGlyphOffset), *font, c});
            }
            pen += metrics.advance;
            //column++;
        }
        if(word) {
            float length=0; for(const ref<Glyph>& word: words) length += advance(word) + spaceAdvance;
            length += width(word); // Last word
            if(wrap && length>wrap && words) nextLine(justify); // would not fit
            words << move(word); penX = 0; subscriptPen=0; // Adds to current line (might be first of new line)
        }
        if(words) nextLine(false); // Clears any remaining words
        //log(bbMin, bbMax, penY, - interline*size /*Reverts last line space*/ + interline*(-font->descender), penY - interline*size /*Reverts last line space*/ + interline*(-font->descender));
        bbMax.y = max(bbMax.y, penY - interline*size /*Reverts last line space*/ + interline*(-font->descender)); // inter widget spacing
    }
};

Text::Text(const string& text, uint size, vec3 color, float alpha, float wrap, string font, bool hint, float interline, bool center)
    : text(toUCS4(text)), size(size), color(color), alpha(alpha), wrap(wrap), font(font), hint(hint), interline(interline), center(center) {}

void Text::layout(float wrap) {
    //textSize=int2(0,size);
    if(center) {
        TextLayout layout(text, size, wrap, font, hint, interline, false); // Layouts without justification
        wrap = glyphs ? layout.bbMax.x : 0;
        assert_(wrap >= 0, wrap);
    }
    TextLayout layout(text, size, wrap, font, hint, interline, true);
    glyphs = move(layout.glyphs);
    textSize = glyphs ? layout.bbMax : 0;
    assert_(textSize > vec2(0), textSize, text);

    /*textLines.clear(); textLines.reserve(layout.text.size);
    cursor=Cursor(0,0); uint currentIndex=0;
    for(const auto& line: layout.text) {
        TextLine textLine;
        for(const TextLayout::Glyph& o: line) {
            currentIndex = o.editIndex;
            if(currentIndex<=editIndex) { // Restores cursor after relayout
                cursor = Cursor(textLines.size, textLine.size);
            }
            textLine << ;
        }
        currentIndex++;
        if(currentIndex<=editIndex) cursor = Cursor(textLines.size, textLine.size); // End of line
        textLines << move(textLine);
    }
    if(!text.size) { assert(editIndex==0); cursor = Cursor(0,0); }
    else if(currentIndex<=editIndex) { assert(textLines); cursor = Cursor(textLines.size-1, textLines.last().size); } // End of text
    links = move(layout.links);
    for(TextLayout::Line& layoutLine: layout.lines) {
        for(uint line: range(layoutLine.begin.line, layoutLine.end.line+1)) {
            TextLayout::TextLine& textLine = layout.text[line];
            if(layoutLine.begin.column<textLine.size) {
                TextLayout::Glyph& first = line==layoutLine.begin.line ? textLine[layoutLine.begin.column] : textLine.first();
                TextLayout::Glyph& last = (line==layoutLine.end.line && layoutLine.end.column<textLine.size) ? textLine[layoutLine.end.column]
                                                                                                                                                                           : textLine.last();
                assert(first.pos.y == last.pos.y);
                lines << Line{ int2(first.pos+vec2(0,1)), int2(last.pos+vec2(last.advance,2))};
            }
        }
    }*/
}
int2 Text::sizeHint(int2 size) {
    /*if(!textSize || (size.x && size.x < textSize.x))*/
    layout(size.x ? min<float>(wrap, size.x) : wrap);
    return max(minimalSizeHint, int2(ceil(textSize)));
}
#if 0
void Text::render() {
    layout(min<float>(wrap, target.size().x));
    render(target, max(int2(0), int2(center ? (target.size().x-textSize.x)/2 : 0, (target.size().y-textSize.y)/2)));
}
void Text::render(const Image& target, int2 offset) {
    /*if(!textSize || target.size().x < textSize.x)*/
    layout(min<float>(wrap, target.size().x)); //FIXME: not when called from render()
    for(const ref<Glyph>& line: glyphs) for(const Glyph& c: line) if(c.image) blit(target, offset+c.pos, c.image, color, alpha);
    //for(const Line& l: lines) fill(target, offset+Rect(l.min,l.max), black);
}
#else
Graphics Text::graphics(int2 size) {
    layout(min<float>(wrap, size.x));
    Graphics graphics;
    vec2 offset = max(vec2(0), vec2(center ? (size.x-textSize.x)/2.f : 0, (size.y-textSize.y)/2.f));
    for(const ref<Glyph>& line: glyphs) for(Glyph e: line) { e.origin += offset; graphics.glyphs << e; }
    return graphics;
}

#endif

#if 0
bool Text::mouseEvent(int2 position, int2 size, Event event, Button button) {
    if(event==Release || (event==Motion && !button)) return false;
    position -= max(int2(0),(size-textSize)/2);
    if(!Rect(textSize).contains(position)) return false;
    for(uint line: range(textLines.size)) {
        if(position.y < (int)(line*this->size) || position.y > (int)(line+1)*this->size) continue;
        const TextLine& textLine = textLines[line];
        if(!textLine) goto break_;
        // Before first glyph
        const Glyph& first = textLine.first();
        if(position.x <= first.center) { cursor = Cursor(line,0); goto break_; }
        // Between glyphs
        for(uint column: range(0,textLine.size-1)) {
            const Glyph& prev = textLine[column];
            const Glyph& next = textLine[column+1];
            if(position.x >= prev.center && position.x <= next.center) { cursor = Cursor(line,column+1); goto break_; }
        }
        // After last glyph
        const Glyph& last = textLine.last();
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
    uint index = 1; // ' ', '\t' or '\n' immediately after last glyph
    uint line=cursor.line;
    while(line>0 && !textLines[line]) line--, index++; //count \n (not included as glyphs)
    if(textLines[line]) index += textLines[line].last().editIndex;
    return index;
}
#endif
