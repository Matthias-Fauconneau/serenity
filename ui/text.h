#pragma once
/// \file text.h Rich text label/paragraph widget
#include "widget.h"
#include "function.h"
#include "image.h"
#include "utf8.h"

/// Rich text format control code encoded in 00-1F range
// \note first word (until ' ') after a Link tag is not displayed but used as \a linkActivated identifier.
enum TextFormat : char {
    Regular,
    Subscript, Superscript, FractionLine, Numerator, Denominator, End, // Nest
    Bold, Italic, // Toggle
    LastTextFormat
};
static_assert(LastTextFormat <= '\t', "");

inline String subscript(const string& s) { return string{TextFormat::Subscript} + s + string{TextFormat::End}; }
inline String superscript(const string& s) { return string{TextFormat::Superscript} + s + string{TextFormat::End}; }
inline String fraction(const string& num, const string& den) {
    return string{TextFormat::FractionLine, TextFormat::Numerator} + num + string{TextFormat::End, TextFormat::Denominator}
               + den + string{TextFormat::End,TextFormat::End};
}
/// Text is a \a Widget displaying text (can be multiple lines)
struct Text : virtual Widget {
    /// Create a caption that display \a text using a \a size pt (points) font
    Text(const string& text=""_, float size=16, vec3 color=0, float alpha=1, float wrap=0, string font="DejaVuSans"_, bool hint=true, float interline=1, bool center=true);

    // Parameters
    /// Displayed text in UTF32
    array<uint> text;
    /// Font size
    int size;
    /// Text color
    vec3 color;
    float alpha;
    /// Line wrap limit in pixels (0: no wrap)
    float wrap = 0;
    /// Font name
    string font;
    /// Whether font should be hinted for display
    bool hint;
    /// Interline stretch
    float interline;
    /// Horizontal alignment
    bool center;
    /// Minimal size hint
    int2 minimalSizeHint=0;

    struct TextLayout layout(float wrap) const;

    int2 sizeHint(int2 size) const override;
    Graphics graphics(int2 size) const override;
};
