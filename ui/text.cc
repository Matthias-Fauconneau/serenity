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
    assert_(!key.contains(' '));
    static map<NameSize,unique<Font>> fonts; // Font cache
    unique<Font>* font = fonts.find(NameSize{copy(key), size});
    return font ? font->pointer
                : fonts.insert(NameSize{copy(key),size},unique<Font>(Map(findFont(fontName, fontTypes), fontFolder()), size, key, hint)).pointer;
}

/// Layouts formatted text with wrapping, justification and links
struct TextLayout {
    struct Glyph : Font::Metrics, ::Glyph {
        //uint editIndex;
        Glyph(const Font::Metrics& metrics, const ::Glyph& glyph) : Font::Metrics(metrics), ::Glyph(glyph) {}
    };
    float width(const ref<Glyph>& word) { float max=0; for(const Glyph& glyph : word) if(glyph.code!=',' && glyph.code!='.') max=::max(max, glyph.origin.x - glyph.bearing.x + glyph.width); return max; }
    float advance(const ref<Glyph>& word) { float max=0; for(const Glyph& glyph : word) max=::max(max, glyph.origin.x + glyph.advance); return max; }

    // Parameters
    float size;
    float wrap;
    float interline;
    float spaceAdvance;

    // Variables
    float penY=0;
    array<array<Glyph>> words;
    vec2 bbMin = 0;
    vec2 bbMax = 0;
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
                    bbMin=min(bbMin, glyph.origin-glyph.bearing);
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
        //int previousRightDelta = 0;
        penY = interline*font->ascender;
        uint i=0;
        for(; i<text.size; i++) {
            uint c = text[i];
            /***/ if(c==' ') penX += spaceAdvance;
            else if(c=='\t') penX += 4*spaceAdvance; //FIXME: align
            else break;
        }
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
                if(bold && italic) font = getFont(fontName, fontSize, {"BoldItalic"_,"RBI"_,"Bold Italic"_}, hint);
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

            /*if(hint) {
                if(previousRightDelta - metrics.leftDelta >= 32) pen--;
                if(previousRightDelta - metrics.leftDelta < -32 ) pen++;
                previousRightDelta = metrics.rightDelta;
            }*/

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
        bbMax.y = max(bbMax.y, penY - interline*size /*Reverts last line space*/ + interline*(-font->descender)); // inter widget spacing
    }
};

Text::Text(const string& text, uint size, vec3 color, float alpha, float wrap, string font, bool hint, float interline, bool center)
    : text(toUCS4(text)), size(size), color(color), alpha(alpha), wrap(wrap), font(font), hint(hint), interline(interline), center(center) {}

TextLayout Text::layout(float wrap) const {
    if(center) {
        TextLayout layout(text, size, wrap, font, hint, interline, false); // Layouts without justification
        assert_(layout.bbMin >= vec2(-2,0));
        wrap = layout.bbMax.x;
        assert_(wrap >= 0, wrap);
    }
    return TextLayout(text, size, wrap, font, hint, interline, true);
}

int2 Text::sizeHint(int2 size) const {
    vec2 textSize = layout(size.x ? min<float>(wrap, size.x) : wrap).bbMax;
    return max(minimalSizeHint, int2(textSize));
}

Graphics Text::graphics(int2 size) const {
    TextLayout layout = this->layout(min<float>(wrap, size.x));
    assert_(layout.bbMin >= vec2(-2,0), layout.bbMin);
    vec2 textSize = layout.bbMax;

    Graphics graphics;
    vec2 offset = max(vec2(0), vec2(center ? (size.x-textSize.x)/2.f : 0, (size.y-textSize.y)/2.f));
    for(const ref<Glyph>& line: layout.glyphs) for(Glyph e: line) { e.origin += offset; graphics.glyphs << e; }
    return graphics;
}
