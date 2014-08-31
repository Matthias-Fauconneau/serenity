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
    float lineOriginY = 0;
    array<array<Glyph>> words;
    //vec2 bbMin = 0;
    vec2 bbMax = 0;

    // Outputs
    array<array<::Glyph>> glyphs;

    void nextLine(bool justify) {
        if(words) {
            float length=0; for(const ref<Glyph>& word: words) length += advance(word); // Sums word lengths
            if(words.last()) length += -advance(words.last()) + width(words.last()); // For last word of line, use last glyph width instead of advance
            float space = (justify && words.size>1) ? (wrap-length)/(words.size-1) : spaceAdvance;

            // Layouts
            float x=0; // Line pen
            auto& line = glyphs.append();
            for(const ref<Glyph>& word: words) {
                for(Glyph glyph: word) {
                    glyph.origin += vec2(x, lineOriginY);
                    bbMax = max(bbMax, glyph.origin-glyph.bearing+glyph.size);
                    line << glyph;
                }
                x += advance(word);
                x += space;
            }
            words.clear();
        }
        lineOriginY += interline*size;
    }

    TextLayout(const ref<uint>& text, float size, float wrap, string fontName, bool hint, float interline, bool justify)
        : size(size), wrap(wrap), interline(interline) {

        Font* font = getFont(fontName, size, {""_,"R"_,"Regular"_}, hint);
        uint16 spaceIndex = font->index(' ');
        spaceAdvance = font->metrics(spaceIndex).advance;

        lineOriginY = interline*font->ascender;

        // Format context
        bool bold=false, italic=false;
        struct Context { TextFormat format, previousFormat; float fontSize; vec2 position, previousPosition; };
        array<Context> stack;
        float fontSize = size;
        TextFormat format = Regular, previousFormat = Regular;
        // Glyph positions
        array<Glyph> word;
        vec2 position = 0, previousPosition = 0;
        // Kerning
        uint16 previous=spaceIndex;
        int previousRightDelta = 0; // Hinted kerning

        uint i=0;
        for(; i<text.size; i++) {
            uint c = text[i];
            /***/ if(c==' ') position.x += spaceAdvance;
            else if(c=='\t') position.x += 4*spaceAdvance; //FIXME: align
            else break;
        }
        for(; i<text.size; i++) {
            uint c = text[i];

            if(c<LastTextFormat) { //00-1F format control flags (bold,italic,underline,strike,link)
                if(c<End) {
                    stack << Context{format, previousFormat, fontSize, position, previousPosition};

                    format = TextFormat(c);
                    if( (format==Superscript && previousFormat==Subscript)
                     || (format==Subscript && previousFormat==Superscript)) // Stack sub/super script
                        position.x = previousPosition.x;
                    if(format==Subscript || format==Superscript) fontSize *= 2./3;

                    previousFormat = Regular;
                    previousPosition = position;
                }
                else if(c==End) {
                    previousFormat = format;
                    Context context = stack.pop();
                    previousPosition = context.position;
                    fontSize = context.fontSize;
                    position.y = context.position.y;
                    format = context.format;
                }
                else if(c==Bold) bold=!bold;
                else if(c==Italic) italic=!italic;
                else error(c);

                if(bold && italic) font = getFont(fontName, fontSize, {"BoldItalic"_,"RBI"_,"Bold Italic"_}, hint);
                else if(bold) font = getFont(fontName, fontSize, {"Bold"_,"RB"_}, hint);
                else if(italic) font = getFont(fontName, fontSize, {"Italic"_,"I"_,"Oblique"_}, hint);
                else font = getFont(fontName, fontSize, {""_,"R"_,"Regular"_}, hint);

                if(c==Subscript) position.y += font->ascender/2;
                if(c==Superscript) position.y -= fontSize/2;

                continue;
            }

            if((c==' '||c=='\t'||c=='\n') && !stack) { // Next word/line
                previous = spaceIndex;
                if(word) {
                    if(words) {
                        float length = 0;
                        for(const ref<Glyph>& word: words) length += advance(word) + spaceAdvance;
                        length += width(word); // Next word
                        if(wrap && length > wrap && words) nextLine(justify); // Would not fit
                    }
                    words << move(word); position.x = 0; // Add to current line (might be first of a new line)
                }
                if(c=='\t') position.x += 4*spaceAdvance; //FIXME: align
                if(c=='\n') nextLine(false);
                continue;
            }
            previousFormat = Regular;

            uint16 index = font->index(c);

            // Kerning
            if(previous != spaceIndex) position.x += font->kerning(previous, index);
            previous = index;

            Font::Metrics metrics = font->metrics(index);

            if(hint) { // Hinted kerning
                if(previousRightDelta - metrics.leftDelta >= 32) position.x -= 1;
                if(previousRightDelta - metrics.leftDelta < -32 ) position.x += 1;
                previousRightDelta = metrics.rightDelta;
            }

            if(c != ' ' && c != 0xA0) {
                vec2 offset = 0;
                if(c==toUCS4("⌊"_)[0] || c==toUCS4("⌋"_)[0]) offset.y += fontSize/3; // Fixes too high floor signs from FreeSerif
                assert_(metrics.size, hex(c));
                word << Glyph(metrics,::Glyph{position+offset, *font, c});
            }
            position.x += metrics.advance;
        }
        if(word) {
            float length=0; for(const ref<Glyph>& word: words) length += advance(word) + spaceAdvance;
            length += width(word); // Last word
            if(wrap && length>wrap && words) nextLine(justify); // would not fit
            words << move(word); // Adds to current line (might be first of new line)
        }
        if(words) nextLine(false); // Clears any remaining words
        bbMax.y = max(bbMax.y, lineOriginY - interline*size /*Reverts last line space*/ + interline*(-font->descender)); // inter widget spacing
    }
};

Text::Text(const string& text, float size, vec3 color, float alpha, float wrap, string font, bool hint, float interline, bool center)
    : text(toUCS4(text)), size(size), color(color), alpha(alpha), wrap(wrap), font(font), hint(hint), interline(interline), center(center) {}

TextLayout Text::layout(float wrap) const {
    if(center) {
        TextLayout layout(text, size, wrap, font, hint, interline, false); // Layouts without justification
        //assert_(layout.bbMin >= vec2(-2,0));
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
    //assert_(layout.bbMin >= vec2(-2,0), layout.bbMin);
    vec2 textSize = layout.bbMax;

    Graphics graphics;
    vec2 offset = max(vec2(0), vec2(center ? (size.x-textSize.x)/2.f : 0, (size.y-textSize.y)/2.f));
    for(const ref<Glyph>& line: layout.glyphs) for(Glyph e: line) { e.origin += offset; graphics.glyphs << e; }
    return graphics;
}
