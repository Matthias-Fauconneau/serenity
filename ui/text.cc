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
    float width(const ref<Glyph>& word) {
        float max=0;
        for(const Glyph& glyph : word) if(glyph.code!=',' && glyph.code!='.') max=::max(max, glyph.origin.x - glyph.bearing.x + glyph.width);
        return max;
    }
    /*float bbWidth(const ref<Glyph>& word) {
        float min=inf, max=-inf;
        for(const Glyph& glyph : word) {
            min=::min(min, glyph.origin.x - glyph.bearing.x);
            max=::max(max, glyph.origin.x - glyph.bearing.x + glyph.width);
        }
        return max-min;
    }*/
    float min(const ref<Glyph>& word) {
        assert_(word);
        float min=inf; for(const Glyph& glyph : word) min=::min(min, glyph.origin.x - glyph.bearing.x); return min;
    }
    float max(const ref<Glyph>& word) {
        assert_(word);
        float max=-inf; for(const Glyph& glyph : word) max=::max(max, glyph.origin.x - glyph.bearing.x + glyph.width); return max;
    }
    float advance(const ref<Glyph>& word) {
        assert_(word);
        float max=-inf; for(const Glyph& glyph : word) max=::max(max, glyph.origin.x + glyph.advance); return max;
    }

    // Parameters
    float size;
    float wrap;
    float interline;
    float spaceAdvance;

    // Variables
    float lineOriginY = 0;
    array<array<Glyph>> words;
    vec2 bbMax = 0;

    // Outputs
    array<array<array<Glyph>>> glyphs;
    array<Line> lines;

    void nextLine(bool justify) {
        if(words) {
            float length=0; for(const ref<Glyph>& word: words) length += advance(word); // Sums word lengths
            if(words.last()) length += -advance(words.last()) + width(words.last()); // For last word of line, use last glyph width instead of advance
            float space = (justify && words.size>1) ? (wrap-length)/(words.size-1) : spaceAdvance;

            // Layouts
            float x=0; // Line pen
            auto& line = glyphs.append();
            for(const ref<Glyph>& word: words) {
                auto& wordOut = line.append();
                for(Glyph glyph: word) {
                    glyph.origin += vec2(x, lineOriginY);
                    bbMax = ::max(bbMax, glyph.origin-glyph.bearing+glyph.size);
                    wordOut.append( glyph );
                }
                x += advance(word);
                x += space;
            }
            words.clear();
        }
        lineOriginY += interline*size;
    }

    void nextWord(array<Glyph>&& word, bool justify) {
        float length = 0;
        for(const ref<Glyph>& word: words) length += advance(word) + spaceAdvance;
        length += width(word); // Last word
        if(wrap && length > wrap && words) nextLine(justify); // Would not fit
        assert_(word);
        words << move(word); // Adds to current line (might be first of a new line)
    }

    TextLayout(const ref<uint>& text, float size, float wrap, string fontName, bool hint, float interline, bool justify)
        : size(size), wrap(wrap), interline(interline) {
        // Fractions
        struct Line { size_t line, word, start, split; };
        array<Line> lines;
        {
            Font* font = getFont(fontName, size, {""_,"R"_,"Regular"_}, hint);
            uint16 spaceIndex = font->index(' ');
            spaceAdvance = font->metrics(spaceIndex).advance;
            float xHeight = font->metrics(font->index('x')).height;

            lineOriginY = interline*font->ascender;

            // Format context
            bool bold=false, italic=false;
            //struct Cursor {size_t line, word, letter; };
            struct Context { TextFormat format; float fontSize; vec2 position; /*Cursor start;*/ size_t start, split; };
            array<Context> stack;
            float fontSize = size;
            TextFormat format = Regular, previousFormat = Regular;
            // Glyph positions
            array<Glyph> word;
            vec2 position = 0, previousPosition = 0;
            // Kerning
            uint16 previous=spaceIndex;
            int previousRightDelta = 0; // Hinted kerning
            // Stack center
            size_t start = 0;
            size_t split = -1;
            //float width = 0;

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
                        if((c==Numerator && previousFormat==Denominator) || (c==Denominator /*&& previousFormat==Numerator*/))
                            split = word.size; // Center
                        if(c==Numerator || c==Regular) start = word.size;
                        stack << Context{format, fontSize, position, start, split};
                        format = TextFormat(c);
                        if((format==Superscript && previousFormat==Subscript) || (format==Subscript && previousFormat==Superscript))
                            position.x = previousPosition.x; // Stack
                        if((format==Numerator && previousFormat==Denominator) || (format==Denominator /*&& previousFormat==Numerator*/))
                            position.x = previousPosition.x; // Stack
                        if(format==Subscript || format==Superscript) fontSize *= 2./3;

                        previousFormat = Regular;
                        previousPosition = position;
                    }
                    else if(c==End) {
                        if(split != invalid && (format == Denominator /*&& (previousFormat==Numerator||previousFormat==Regular)*//*|| format == Numerator*/)) {
                            assert_(start < word.size && start < split && split < word.size, start, split, toUTF8(text));
                            mref<Glyph> word1 = word(start, split), word2 = word.slice(split);
                            assert_(word1 && word2, word.size, word1.size, word2.size, start, split);
                            float delta = min(word1)-min(word2) + ((max(word1)-min(word1))-(max(word2)-min(word2)))/2;
                            if(delta > 0) for(auto& e: word2) e.origin.x += delta;
                            else for(auto& e: word1) e.origin.x -= delta;
                        }
                        previousFormat = format;
                        Context context = stack.pop();
                        if(format == FractionLine) {
                            assert_(start < word.size && start < split && split < word.size, start, split, toUTF8(text));
                            lines << Line{glyphs.size, words.size, start, split};
                        }
                        previousPosition = context.position;
                        fontSize = context.fontSize;
                        position.y = context.position.y;
                        format = context.format;
                        start = context.start;
                        split = context.split;
                    }
                    else if(c==Bold) bold=!bold;
                    else if(c==Italic) italic=!italic;
                    else error(c);

                    if(bold && italic) font = getFont(fontName, fontSize, {"BoldItalic"_,"RBI"_,"Bold Italic"_}, hint);
                    else if(bold) font = getFont(fontName, fontSize, {"Bold"_,"RB"_}, hint);
                    else if(italic) font = getFont(fontName, fontSize, {"Italic"_,"I"_,"Oblique"_}, hint);
                    else font = getFont(fontName, fontSize, {""_,"R"_,"Regular"_}, hint);

                    if(c==Numerator) position.y += -xHeight - (-font->descender)*(1+2./3+4./9);
                    if(c==Superscript) position.y -= fontSize/2;
                    if(c==Subscript) position.y += font->ascender/2;
                    if(c==Denominator) position.y += -xHeight + font->ascender +  -(font->descender)*(2./3+4./9) /*VAlign*/;

                    continue;
                }

                if(c==' '||c=='\t'||c=='\n') { // Next word/line
                    previous = spaceIndex;
                    if(!stack) {
                        if(word) nextWord(move(word), justify);
                        position.x = 0;
                        if(c=='\t') position.x += 4*spaceAdvance; //FIXME: align
                        if(c=='\n') nextLine(false);
                    } else {
                        assert_(c==' ');
                        position.x += spaceAdvance;
                    }
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
            if(word) nextWord(move(word), false);
            nextLine(false);
            bbMax.y = ::max(bbMax.y, lineOriginY - interline*size /*Reverts last line space*/ + interline*(-font->descender)); // inter widget spacing
        }

        for(auto& line: lines) {
            const auto& word = glyphs[line.line][line.word];
            assert_(line.start < word.size && line.split < word.size, "line"_, line.start, line.split);
            const auto& fract = word.slice(line.start);
            const auto& num = word.slice(line.start, line.split);
            const auto& den = word.slice(line.split);
            float numMaxY = ::max(apply(num, [](const Glyph& g) { return g.origin.y - g.bearing.y + g.height; }));
            float denMinY = ::min(apply(den, [](const Glyph& g) { return g.origin.y - g.bearing.y; }));
            float midY = (numMaxY+denMinY) / 2;
            float minX  = ::min(apply(fract, [](const Glyph& g) { return g.origin.x - g.bearing.x; }));
            float maxX  = ::max(apply(fract, [](const Glyph& g) { return g.origin.x - g.bearing.x + g.width; }));
            this->lines << ::Line{vec2(minX, midY), vec2(maxX, midY)};
        }
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
    for(const auto& line: layout.glyphs) for(const auto& word: line) for(Glyph e: word) { e.origin += offset; graphics.glyphs << e; }
    for(auto e: layout.lines) { e.a += offset; e.b +=offset; graphics.lines << e; }
    return graphics;
}
