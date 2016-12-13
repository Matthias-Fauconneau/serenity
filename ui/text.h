#pragma once
/// \file text.h Rich text label/paragraph widget
#include "widget.h"
#include "function.h"
#include "image.h"
#include "utf8.h"

/// Rich text format control code encoded in 00-1F range
enum class TextFormat { Begin=14, Regular=Begin, Bold, Italic, Superscript, Subscript, Stack, Fraction, Link, Color, End };

inline String regular(string s) { return char(TextFormat::Regular) + s + char(TextFormat::End); }
inline String bold(string s) { return char(TextFormat::Bold) + s + char(TextFormat::End); }
inline String italic(string s) { return char(TextFormat::Italic) + s + char(TextFormat::End); }
inline String superscript(string s) { return char(TextFormat::Superscript) + s + char(TextFormat::End); }
inline String subscript(string s) { return char(TextFormat::Subscript) + s + char(TextFormat::End); }
inline String stack(string s) { return char(TextFormat::Stack) + s + char(TextFormat::End); }
inline String fraction(string s) { return char(TextFormat::Fraction) + s + char(TextFormat::End); }
inline String link(string s, string id) { return char(TextFormat::Link) + id + '\0' + s + char(TextFormat::End); }
inline buffer<uint> link(string s, ref<uint> id) { return uint(TextFormat::Link) + id + 0u + toUCS4(s) + uint(TextFormat::End); }
inline buffer<uint> color(ref<uint> s, bgr3f bgr) { return uint(TextFormat::Color) + cast<uint>((ref<float>)bgr) + s + uint(TextFormat::End); }
inline buffer<uint> color(string s, bgr3f bgr) { return s ? color(toUCS4(s), bgr) : buffer<uint>(); }

struct Cursor {
    size_t line=0, column=0;
    Cursor(){}
    Cursor(size_t line, size_t column) : line(line), column(column) {}
    bool operator ==(const Cursor& o) const { return line==o.line && column==o.column; }
    bool operator <(const Cursor& o) const { return line<o.line || (line==o.line && column<o.column); }
    bool operator <=(const Cursor& o) const { return line<o.line || (line==o.line && column<=o.column); }
};
struct Link { Cursor begin,end; buffer<uint> identifier;};

/// Layouts formatted text with wrapping, justification and links
struct TextLayout {
    // Parameters
    float size = 0;
    float wrap = 0;
    float interline = 0;
    float spaceAdvance = 0;
    int align = 0;

    // Variables
    float lineOriginY = 0;
    struct Glyph : Font::Metrics, ::Glyph {
        size_t sourceIndex;
        Glyph(const Font::Metrics& metrics, const ::Glyph& glyph, size_t sourceIndex)
            : Font::Metrics(metrics), ::Glyph(glyph), sourceIndex(sourceIndex) {}
    };
    array<array<Glyph>> words;
    vec2 bbMin = 0, bbMax = 0;

    // Outputs
    array<array<array<Glyph>>> glyphs;
    array<Line> lines;
    array<Link> links;

    TextLayout() {}
    TextLayout(const ref<uint> text, float size, float wrap, string fontName, bool hint, float interline, int align,
               bool justify, bool justifyExplicit, bool justifyLast, bgr3f color);

    operator bool() const { return size; }

    void nextLine(bool justify, int align);
    void nextWord(array<Glyph>&& word, bool justify);

    // Bounding box for stacking
    vec2 min(const ref<Glyph> word);
    vec2 max(const ref<Glyph> word);
    // Length for justification
    float width(const ref<Glyph> word);
    float advance(const ref<Glyph> word);
};

struct EditStop { float left, center, right; size_t sourceIndex; };
array<EditStop> lineStops(ref<array<TextLayout::Glyph>> line);

/// Text is a \a Widget displaying text (can be multiple lines)
struct Text : virtual Widget {
    /// Create a caption that display \a text using a \a size pixel font
    Text(buffer<uint>&& text, float size=16, bgr3f color=0, float opacity=1, float wrap=0, string font="DejaVuSans"_, bool hint=true,
         float interline=1, int2 align=-1, int2 minimalSizeHint=0, bool justify = false, bool justifyExplicitLineBreak = false);
    /// Create a caption that display \a text using a \a size pixel font
    Text(const string text="", float size=16, bgr3f color=0, float opacity=1, float wrap=0, string font="DejaVuSans"_, bool hint=true,
         float interline=1, int2 align=-1, int2 minimalSizeHint=0, bool justify = false, bool justifyExplicitLineBreak = false) :
        Text(toUCS4(text), size, color, opacity, wrap, font, hint, interline, align, minimalSizeHint, justify, justifyExplicitLineBreak) {}

    /// Displayed text in UCS4
    array<uint> text;
    // Parameters
    /// Font size
    int size;
    /// Text color
    bgr3f color;
    /// Text opacity
    float opacity;
    /// Line wrap limit in pixels (0: no wrap)
    float wrap = 0;
    /// Font name
    string font;
    /// Whether font should be hinted for display
    bool hint;
    /// Interline stretch
    float interline;
    /// Alignment
    int2 align;
    /// Whether to justify
    bool justify;
    /// Whether to justify explicit line breaks
    bool justifyExplicitLineBreak;
    /// Minimal size hint
    vec2 minimalSizeHint;
    /// User activated a link
    function<void(ref<uint>)> linkActivated;

    /// Caches last text layout (for a given wrap)
    TextLayout lastTextLayout;

    const TextLayout& layout(float wrap=0);
    vec2 sizeHint(vec2 size=0) override;
    shared<Graphics> graphics(vec2 size) override;
    Cursor cursorFromPosition(vec2 size, vec2 position);
    bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus /*FIXME: -> Window& window*/) override;
};
