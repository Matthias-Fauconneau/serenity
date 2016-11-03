#include "text.h"
#include "graphics.h"
#include "font.h"
#include "utf8.h"

vec2 TextLayout::min(const ref<Glyph> word) {
    assert_(word);
    vec2 min = inff;
    for(const Glyph& g : word) min=::min(min, g.origin - g.bearing);
    return min;
}
vec2 TextLayout::max(const ref<Glyph> word) {
    assert_(word);
    vec2 max=-inff;
    for(const Glyph& g : word) max=::max(max, g.origin - g.bearing + g.size);
    return max;
}
float TextLayout::width(const ref<Glyph> word) {
    float max=0;
    for(const Glyph& g : word) if(g.code!=',' && g.code!='.') max=::max(max, g.origin.x - g.bearing.x + g.width);
    return max;
}
float TextLayout::advance(const ref<Glyph> word) {
    if(!word) return 0;
    float max=-inff; for(const Glyph& g : word) max=::max(max, g.origin.x + g.advance); return max;
}

void TextLayout::nextLine(bool justify, int align) {
    if(words) {
        float length=0; for(const ref<Glyph> word: words) length += advance(word); // Sums word lengths
        if(words.last()) length += -advance(words.last()) + width(words.last()); // For last word of line, use last glyph width instead of advance
        float space = (justify && words.size>1) ? (wrap-length)/(words.size-1) : spaceAdvance;

        // Layouts
        float x = 0;
        if(align == 1) x += wrap-(length+(words.size-1)*space); // Aligns right before wrap
        if(align == 0) x -= (length+(words.size-1)*space)/2; // Centers around 0
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
    } else glyphs.append(); // Empty line for TextEdit
    lineOriginY += interline*size;
}

void TextLayout::nextWord(array<Glyph>&& word, bool justify) {
    float length = 0;
    for(const ref<Glyph> word: words) length += advance(word) + spaceAdvance;
    length += width(word); // Last word
    if(wrap && length > wrap && words) nextLine(justify, this->align); // Would not fit
    assert_(word);
    words.append( move(word) ); // Adds to current line (might be first of a new line)
}

TextLayout::TextLayout(const ref<uint> text, float size, float wrap, string fontName, bool hint, float interline, int align,
                       bool justify, bool justifyExplicit, bool justifyLast, bgr3f color) : size(size), wrap(wrap), interline(interline), align(align) {
    if(!text) return;
    // Fraction lines
    struct Context {
        TextFormat format; FontData* font; float size; bgr3f color; vec2 origin; size_t start; array<Context> children; vec2 position; size_t end;
        void translate(vec2 offset) {
            origin += offset; position += offset;
            for(auto& e: children) e.translate(offset);
        }
    };
    struct Line { size_t line, word; size_t start, split, end; };
    array<Line> lines;
    {
        FontData* font = getFont(fontName, {"","R","Regular"});
        uint16 spaceIndex = font->font(size, hint).index(' ');
        spaceAdvance = font->font(size, hint).metrics(spaceIndex).advance;

        lineOriginY = interline*font->font(size, hint).ascender;

        // Glyph positions
        array<Glyph> word;

        // Format context
        array<Context> stack;
        TextFormat format = TextFormat::Regular;
        vec2 origin = 0;
        size_t start = 0;
        array<Context> children;
        vec2 position = 0;
        uint column = 0;
        Link link;

        // Kerning
        uint16 previous=spaceIndex;
        int previousRightOffset = 0; // Hinted kerning

        for(size_t sourceIndex=0; sourceIndex<text.size; sourceIndex++) {
            uint c = text[sourceIndex];
            // Breaking whitespace
            /***/ if(c==' '||c=='\t'||c=='\n') {
                previous = spaceIndex;
                auto parentFormats = apply(stack,[](const Context& c){return c.format;});
                /***/ if(!parentFormats.contains(TextFormat::Stack) && !parentFormats.contains(TextFormat::Fraction) /*&& (word || words || glyphs)*/) {
                    if(!justify && c==' ') { // Includes whitespace "Glyph" placeholders for editing
                        word.append(Glyph({spaceAdvance,0,0,0,{{0,0}}},{position, size, *font, c, font->font(size).index(c), color}, sourceIndex));
                        position.x += spaceAdvance;
                        column++;
                    }
                    if((justify && c==' ') || c=='\n') { // Splits into words for justification
                        if(word) nextWord(move(word), justify);
                        position.x = 0;
                        column++;
                    }
                    if(c=='\n') {
                        nextLine(justifyExplicit, this->align);
                        column = 0;
                    }
                    if(c=='\t') {
                        word.append(Glyph({4*spaceAdvance,0,0,0,{{0,0}}},{position, size, *font, c, font->font(size).index(c), color}, sourceIndex));
                        position.x += 4*spaceAdvance;
                        column++;
                    }
                }
                else if(c==' ') position.x += spaceAdvance;
                else error("Unexpected code", hex(c), toUTF8(text));
            }
            // Push format context
            else if(TextFormat(c)>=TextFormat::Begin && TextFormat(c)<TextFormat::End) {
                stack.append( Context{format, font, size, color, origin, start, move(children), position, word.size} );
                start = word.size;
                origin = position;
                //String fontName = copy(font->name);
                format = TextFormat(c);
                if(format==TextFormat::Bold) { assert_(!find(fontName,"Bold"), toUTF8(text)); font = getFont(fontName, {"Bold","RB"}); }
                else if(format==TextFormat::Italic) { assert_(!find(fontName,"Italic")); font = getFont(fontName, {"Italic","I","Oblique"}); }
                else if(format==TextFormat::Subscript || format==TextFormat::Superscript) {
                    size *= 2./3;
                    if(format==TextFormat::Superscript) position.y -= size/2; ///4;
                    else /*format==TextFormat::Subscript*/ position.y += font->font(size, hint).ascender/3;
                } else if(format==TextFormat::Color) {
                    color = bgr3f(*(float*)&text[sourceIndex+1], *(float*)&text[sourceIndex+2], *(float*)&text[sourceIndex+3]);
                    sourceIndex += 3;
                }
                else if(format==TextFormat::Link) {
                    sourceIndex++;
                    size_t start = sourceIndex;
                    while(text[sourceIndex]) sourceIndex++;
                    link.identifier = copyRef(text.sliceRange(start, sourceIndex));
                    // 0
                    link.begin = {glyphs.size, column};
                }
                else if(format==TextFormat::Stack || format==TextFormat::Fraction) {}
                else if(format==TextFormat::Regular) {}
                else error("Unknown format", uint(format));
            }
            // Pop format context
            else if(TextFormat(c)==TextFormat::End) {
                if(format==TextFormat::Link) {
                    link.end = {glyphs.size, column};
                    links.append(move(link));
                    link = Link();
                }
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
                        float margin = format == TextFormat::Fraction ? size/3 : 0;
                        {vec2 offset (0, size0.y/2 + margin); children[0].translate(-offset); for(auto& e: word0) e.origin -= offset;}
                        {vec2 offset (0, size1.y/2 + margin); children[1].translate(-offset); for(auto& e: word1) e.origin += offset;}
                    }
                }
                if(format == TextFormat::Fraction) {
                    assert_(children.size==2 && children[0].end == children[1].start, children.size);
                    lines.append( Line({glyphs.size, words.size, children[0].start, children[0].end, children[1].end}) );
                }
                Context child = {format, font, size, color, origin, start, move(children), position, word.size};
                Context context = stack.pop();
                format = context.format;
                font = context.font;
                size = context.size;
                color = context.color;
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
                uint16 index = font->font(size).index(c);
                Font::Metrics metrics = font->font(size).metrics(index);
                // Kerning
                if(previous != spaceIndex) position.x += font->font(size).kerning(previous, index);
                previous = index;
                if(hint) {
                    if(previousRightOffset - metrics.leftOffset >= 32) position.x -= 1;
                    if(previousRightOffset - metrics.leftOffset < -32 ) position.x += 1;
                    previousRightOffset = metrics.rightOffset;
                }
                if(c != 0xA0) {
                    vec2 offset = 0;
                    //if(c==toUCS4("⌊")[0] || c==toUCS4("⌋")[0]) offset.y += size/3; // Fixes too high floor signs from FreeSerif
                    //assert_(metrics.size, hex(c));
                    //assert_(c < 127, c, hex(c), text);
                    word.append( Glyph(metrics,::Glyph{position+offset, size, *font, c, font->font(size).index(c), color}, sourceIndex) );
                    column++;
                }
                position.x += metrics.advance;
            }
        }
        if(word) nextWord(move(word), justify);
        nextLine(justifyLast && glyphs.size>1/*if multiple lines*/, this->align);
        bbMax.y = ::max(bbMax.y, lineOriginY - interline*size /*Reverts last line space*/ + interline*(-font->font(size).descender)); // inter widget spacing
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

Text::Text(buffer<uint>&& text, float size, bgr3f color, float opacity, float wrap, string font, bool hint, float interline, int2 align, int2 minimalSizeHint,
           bool justify, bool justifyExplicitLineBreak)
    : text(move(text)), size(size), color(color), opacity(opacity), wrap(wrap), font(font), hint(hint), interline(interline), align(align),
      justify(justify), justifyExplicitLineBreak(justifyExplicitLineBreak), minimalSizeHint(minimalSizeHint) {}

const TextLayout& Text::layout(float wrap) {
    wrap = max(0.f, wrap);
    assert_(wrap >= 0, wrap);
    if(!lastTextLayout || wrap != lastTextLayout.wrap)
        lastTextLayout = TextLayout(text, size, wrap, font, hint, interline, align.x, justify, justifyExplicitLineBreak, true, color);
    return lastTextLayout;
}

vec2 Text::sizeHint(vec2 size) {
    const TextLayout& layout = this->layout(size.x ? min<float>(wrap, size.x) : wrap);
    return max(minimalSizeHint, ceil(layout.bbMax - layout.bbMin));
}

shared<Graphics> Text::graphics(vec2 size) {
    const TextLayout& layout = this->layout(size.x ? min<float>(wrap, size.x) : wrap);
    vec2 textSize = ceil(layout.bbMax - /*min(vec2(0),*/layout.bbMin/*)*/);
    vec2 offset = /*max(vec2(0),*/ vec2(align.x==0 ? size.x/2 : (size.x-textSize.x)/2.f, /*align.y==0 ? size.y/2 :*/ (size.y-textSize.y)/2.f);//);
    //if(align == -1) offset.x = 0;

    shared<Graphics> graphics;
    //FIXME: use Graphic::offset, cache graphics
    for(const auto& line: layout.glyphs) for(const auto& word: line) for(Glyph e: word) { e.origin += offset; graphics->glyphs.append( e ); }
    for(auto e: layout.lines) { e.a += offset; e.b +=offset; graphics->lines.append( e ); }
    graphics->bounds = Rect(offset,offset+textSize);
    return graphics;
}

array<EditStop> lineStops(ref<array<TextLayout::Glyph>> line) {
    array<EditStop> stops;
    for(const auto& word: line) {
        if(stops) { // Justified space
            float left = stops.last().right, right = word[0].origin.x, center = (left+right)/2;
            assert_(stops.last().sourceIndex+1 == word[0].sourceIndex-1);
            //assert_(text[stops.last().sourceIndex+1]==' ');
            stops.append({left, center, right, stops.last().sourceIndex+1});
        }
        for(auto& glyph: word) {
            float left = glyph.origin.x, right = left + glyph.advance, center = (left+right)/2;
            //assert_(right > left);
            stops.append({left, center, right, glyph.sourceIndex});
        }
    }
    return stops;
}

Cursor Text::cursorFromPosition(vec2 size, vec2 position) {
    const TextLayout& layout = this->layout(size.x ? min<float>(wrap, size.x) : wrap);
    vec2 textSize = ceil(layout.bbMax - min(vec2(0),layout.bbMin));
    vec2 offset = max(vec2(0), vec2(align.x==0 ? size.x/2 : (size.x-textSize.x)/2.f, align.y==0 ? size.y/2 : (size.y-textSize.y)/2.f));
    position -= offset;
    if(position.y < 0) return {0, 0};
    const auto& lines = lastTextLayout.glyphs;
    for(size_t lineIndex: range(lines.size)) {
        if(position.y < (lineIndex*this->size) || position.y > (lineIndex+1)*this->size) continue; // FIXME: Assumes uniform line height
        const auto line = lineStops(lines[lineIndex]);
        if(!line) return {lineIndex, line.size};
        // Before first stop
        if(position.x <= line[0].center) return {lineIndex, 0};
        // Between stops
        for(size_t stop: range(0, line.size-1)) {
            if(position.x >= line[stop].center && position.x <= line[stop+1].center) return {lineIndex, stop+1};
        }
        // After last stop
        if(position.x >= line.last().center) return {lineIndex, line.size};
    }
    if(!lines) return {0,0};
    return {lines.size-1, lineStops(lines.last()).size};
}

bool Text::mouseEvent(vec2 position, vec2 size, Event event, Button button, Widget*&) {
    if(event==Release && button==LeftButton) {
        Cursor cursor = cursorFromPosition(size, position);
        for(const Link& link: lastTextLayout.links) {
            if(link.begin<=cursor && cursor<link.end) { linkActivated(link.identifier); return true; }
        }
    }
    return false;
}
