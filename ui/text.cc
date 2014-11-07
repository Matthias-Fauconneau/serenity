#include "text.h"
#include "graphics.h"
#include "font.h"
#include "utf8.h"

struct NameSize { String name; float size; };
bool operator ==(const NameSize& a, const NameSize& b) { return a.name == b.name && a.size == b.size; }
string str(const NameSize& x) { return str(x.name, x.size); }

/// Returns a font, loading from disk and caching as needed
Font* getFont(string fontName, float size, ref<string> fontTypes, bool hint) {
	String key = fontName+(hint?"H"_:""_)+fontTypes[0];
    assert_(!key.contains(' '));
    static map<NameSize,unique<Font>> fonts; // Font cache
    unique<Font>* font = fonts.find(NameSize{copy(key), size});
    if(font) return font->pointer;
    String path = findFont(fontName, fontTypes);
	String name = copyRef(section(section(path, '/', -2, -1),'.'));
    return fonts.insert(NameSize{copy(key),size}, unique<Font>(Map(path), size, name, hint)).pointer;
}

/// Layouts formatted text with wrapping, justification and links
struct TextLayout {
    struct Glyph : Font::Metrics, ::Glyph {
        Glyph(const Font::Metrics& metrics, const ::Glyph& glyph) : Font::Metrics(metrics), ::Glyph(glyph) {}
    };
    // Bounding box for stacking
    vec2 min(const ref<Glyph> word) {
        assert_(word);
        vec2 min=inf;
        for(const Glyph& g : word) min=::min(min, g.origin - g.bearing);
        return min;
    }
    vec2 max(const ref<Glyph> word) {
        assert_(word);
        vec2 max=-inf;
        for(const Glyph& g : word) max=::max(max, g.origin - g.bearing + g.size);
        return max;
    }
    // Length for justification
    float width(const ref<Glyph> word) {
        float max=0;
        for(const Glyph& g : word) if(g.code!=',' && g.code!='.') max=::max(max, g.origin.x - g.bearing.x + g.width);
        return max;
    }
    float advance(const ref<Glyph> word) {
        assert_(word);
        float max=-inf; for(const Glyph& g : word) max=::max(max, g.origin.x + g.advance); return max;
    }

    // Parameters
    float size;
    float wrap;
    float interline;
    float spaceAdvance;

    // Variables
    float lineOriginY = 0;
    array<array<Glyph>> words;
    vec2 bbMin = 0, bbMax = 0;

    // Outputs
    array<array<array<Glyph>>> glyphs;
    array<Line> lines;

    void nextLine(bool justify) {
        if(words) {
            float length=0; for(const ref<Glyph> word: words) length += advance(word); // Sums word lengths
            if(words.last()) length += -advance(words.last()) + width(words.last()); // For last word of line, use last glyph width instead of advance
            float space = (justify && words.size>1) ? (wrap-length)/(words.size-1) : spaceAdvance;

            // Layouts
            float x=0; // Line pen
            auto& line = glyphs.append();
            for(const ref<Glyph> word: words) {
                auto& wordOut = line.append();
                for(Glyph glyph: word) {
                    glyph.origin += vec2(x, lineOriginY);
                    bbMin = ::min(bbMin, glyph.origin-glyph.bearing);
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
        for(const ref<Glyph> word: words) length += advance(word) + spaceAdvance;
        length += width(word); // Last word
        if(wrap && length > wrap && words) nextLine(justify); // Would not fit
        assert_(word);
		words.append( move(word) ); // Adds to current line (might be first of a new line)
    }

    TextLayout(const ref<uint> text, float size, float wrap, string fontName, bool hint, float interline, bool justify, bool justifyExplicit, bool justifyLast,
			   bgr3f color) : size(size), wrap(wrap), interline(interline) {
        // Fraction lines
        struct Context {
            TextFormat format; Font* font; vec2 origin; size_t start; array<Context> children; vec2 position; size_t end;
            //mref<Glyph> word(const mref<Glyph>& word) const { return word(children[0].start, children[0].end); }
            void translate(/*const mref<Glyph>& word, */vec2 offset) {
                origin += offset; position += offset;
                for(auto& e: children) e.translate(offset);
                //for(auto& e: this->word(word)) e.origin.x += offset;
            }
        };
        struct Line { size_t line, word; size_t start, split, end; };
        array<Line> lines;
        {
            Font* font = getFont(fontName, size, {"","R","Regular"}, hint);
            uint16 spaceIndex = font->index(' ');
            spaceAdvance = font->metrics(spaceIndex).advance;
            //float xHeight = font->metrics(font->index('x')).height;

            lineOriginY = interline*font->ascender;

            // Glyph positions
            array<Glyph> word;

            // Format context
            array<Context> stack;
			TextFormat format = TextFormat::Regular;
            vec2 origin = 0;
            size_t start = 0;
            array<Context> children;
            vec2 position = 0;

            // Kerning
            uint16 previous=spaceIndex;
            int previousRightOffset = 0; // Hinted kerning

            for(uint c: text) {
                // Breaking whitespace
                /***/ if(c==' '||c=='\t'||c=='\n') {
                    previous = spaceIndex;
                    auto parentFormats = apply(stack,[](const Context& c){return c.format;});
					/***/ if(!parentFormats.contains(TextFormat::Stack) && !parentFormats.contains(TextFormat::Fraction) && (word || words || glyphs)) {
                        if(word) {
                            nextWord(move(word), justify);
                            position.x = 0;
                        }
                        if(c=='\n') nextLine(justifyExplicit);
                        if(c=='\t') position.x += 4*spaceAdvance; //FIXME: align
                    }
                    else if(c==' ') position.x += spaceAdvance;
                    else if(c=='\t') position.x += 4*spaceAdvance; //FIXME: align
                    else error("Unexpected code", hex(c), toUTF8(text));
                }
                // Push format context
				else if(TextFormat(c)<TextFormat::End) {
					stack.append( Context{format, font, origin, start, move(children), position, word.size} );
                    start = word.size;
                    origin = position;
                    String fontName = copy(font->name);
                    float fontSize = font->size;
                    format = TextFormat(c);
					if(format==TextFormat::Bold) { assert_(!find(fontName,"Bold"), toUTF8(text)); font = getFont(fontName, fontSize, {"Bold","RB"}, hint); }
					if(format==TextFormat::Italic) { assert_(!find(fontName,"Italic")); font = getFont(fontName, fontSize, {"Italic","I","Oblique"}, hint); }
					if(format==TextFormat::Subscript || format==TextFormat::Superscript) fontSize *= 2./3;
					if(format==TextFormat::Superscript) position.y -= fontSize/2;
					if(format==TextFormat::Subscript) position.y += font->ascender/2;
                }
                // Pop format context
				else if(TextFormat(c)==TextFormat::End) {
					if(format == TextFormat::Stack || format == TextFormat::Fraction) {
                        assert_(children.size==2 && children[0].end == children[1].start, children.size);
                        auto flatten = [](Context& c) {
							while(c.children.size == 1 && c.format==TextFormat::Regular && c.start==c.children[0].start && c.children[0].end==c.end)
                                c = move(c.children[0]);
                        };
                        flatten(children[0]);
                        flatten(children[1]);
                        assert(children[0].start < children[0].end && children[0].end < word.size, children[0].start, children[0].end, word.size, toUTF8(text));
						mref<Glyph> word0 = word.sliceRange(children[0].start, children[0].end);
						mref<Glyph> word1 = word.sliceRange(children[1].start, children[1].end);
                        assert_(word0 && word1, word.size, word0.size, word1.size);
                        vec2 min0 = min(word0), max0 = max(word0), size0 = max0-min0;
                        vec2 min1 = min(word1), max1 = max(word1), size1 = max1-min1;
                        { // Horizontal center align
                            vec2 offset ( ((min0-min1)/2.f + (size0-size1)/2.f).x, 0);
                            if(offset.x < 0) { children[0].translate(-offset); for(auto& e: word0) e.origin -= offset; }
                            else                 { children[1].translate( offset); for(auto& e: word1) e.origin += offset; }
                        }
						if(/*children[0].format == Regular &&*/ children[1].format==TextFormat::Subscript) { // Regular over Subscript
                            {vec2 offset (0, size1.y/2); children[1].translate(-offset); for(auto& e: word1) e.origin += offset;}
                        }
                        else if(children[0].format == children[1].format) { // Vertical even share
							float margin = format == TextFormat::Fraction ? font->size/3 : 0;
                            {vec2 offset (0, size0.y/2 + margin); children[0].translate(-offset); for(auto& e: word0) e.origin -= offset;}
                            {vec2 offset (0, size1.y/2 + margin); children[1].translate(-offset); for(auto& e: word1) e.origin += offset;}
                        }
                    }
					if(format == TextFormat::Fraction) {
                        assert_(children.size==2 && children[0].end == children[1].start, children.size);
						lines.append( Line({glyphs.size, words.size, children[0].start, children[0].end, children[1].end}) );
                    }
                    Context child = {format, font, origin, start, move(children), position, word.size};
                    Context context = stack.pop();
                    format = context.format;
                    font = context.font;
                    origin = context.origin;
                    start = context.start;
                    children = move(context.children);
					children.append( move(child) );
                    position.y = context.position.y;
					if(format == TextFormat::Stack || format == TextFormat::Fraction) {
                        assert_(children.size<=2);
                        if(children.size==1) position.x = context.position.x;
                    }
                }
                // Glyph
                else {
                    uint16 index = font->index(c);
                    Font::Metrics metrics = font->metrics(index);
                    // Kerning
                    if(previous != spaceIndex) position.x += font->kerning(previous, index);
                    previous = index;
                    if(hint) {
                        if(previousRightOffset - metrics.leftOffset >= 32) position.x -= 1;
                        if(previousRightOffset - metrics.leftOffset < -32 ) position.x += 1;
                        previousRightOffset = metrics.rightOffset;
                    }
                    if(c != 0xA0) {
                        vec2 offset = 0;
                        if(c==toUCS4("⌊")[0] || c==toUCS4("⌋")[0]) offset.y += font->size/3; // Fixes too high floor signs from FreeSerif
                        assert_(metrics.size, hex(c));
						word.append( Glyph(metrics,::Glyph{position+offset, *font, c, font->index(c), color}) );
                    }
                    position.x += metrics.advance;
                }
            }
            if(word) nextWord(move(word), justify);
            nextLine(justifyLast && glyphs.size>1/*if multiple lines*/);
            bbMax.y = ::max(bbMax.y, lineOriginY - interline*size /*Reverts last line space*/ + interline*(-font->descender)); // inter widget spacing
        }

        for(auto& line: lines) {
            const auto& word = glyphs[line.line][line.word];
            assert_(line.start < line.split && line.split < line.end && line.end <= word.size, line.start, line.split, line.end, word.size, toUTF8(text));
			const auto& fract = word.sliceRange(line.start, line.end);
			const auto& num = word.sliceRange(line.start, line.split);
			const auto& den = word.sliceRange(line.split,line.end);
            float numMaxY = ::max(apply(num, [](const Glyph& g) { return g.origin.y - g.bearing.y + g.height; }));
            float denMinY = ::min(apply(den, [](const Glyph& g) { return g.origin.y - g.bearing.y; }));
            float midY = (numMaxY+denMinY) / 2;
            float minX  = ::min(apply(fract, [](const Glyph& g) { return g.origin.x - g.bearing.x; }));
            float maxX  = ::max(apply(fract, [](const Glyph& g) { return g.origin.x - g.bearing.x + g.width; }));
			this->lines.append( ::Line{vec2(minX, midY), vec2(maxX, midY), black} );
        }
    }
};

Text::Text(const string text, float size, bgr3f color, float opacity, float wrap, string font, bool hint, float interline, bool center, int2 minimalSizeHint)
    : text(toUCS4(text)), size(size), color(color), opacity(opacity), wrap(wrap), font(font), hint(hint), interline(interline), center(center),
      minimalSizeHint(minimalSizeHint) {}

TextLayout Text::layout(float wrap) const {
    if(center) {
        TextLayout layout(text, size, wrap, font, hint, interline, false, false, false, color); // Layouts without justification
        wrap = layout.bbMax.x;
        assert_(wrap >= 0, wrap);
    }
    return TextLayout(text, size, wrap, font, hint, interline, true, true, true, color);
}

int2 Text::sizeHint(int2 size) {
    TextLayout layout = this->layout(size.x ? min<float>(wrap, size.x) : wrap);
    vec2 textSize = ceil(layout.bbMax - min(vec2(0),layout.bbMin));
    return max(minimalSizeHint, int2(textSize));
}

shared<Graphics> Text::graphics(int2 size) {
    TextLayout layout = this->layout(min<float>(wrap, size.x));
    vec2 textSize = ceil(layout.bbMax - min(vec2(0),layout.bbMin));

	shared<Graphics> graphics;
    //assert_(abs(size.y - textSize.y)<=1, size, textSize);
    vec2 offset = max(vec2(0), vec2(center ? (size.x-textSize.x)/2.f : 0, (size.y-textSize.y)/2.f));
	//FIXME: use Graphic::offset
	for(const auto& line: layout.glyphs) for(const auto& word: line) for(Glyph e: word) { e.origin += offset; graphics->glyphs.append( e ); }
	for(auto e: layout.lines) { e.a += offset; e.b +=offset; graphics->lines.append( e ); }
    return graphics;
}
