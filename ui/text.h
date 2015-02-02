#pragma once
/// \file text.h Rich text label/paragraph widget
#include "widget.h"
#include "function.h"
#include "core/image.h"
#include "utf8.h"

/// Rich text format control code encoded in 00-1F range
// \note first word (until ' ') after a Link tag is not displayed but used as \a linkActivated identifier.
enum class TextFormat : char { Regular, Bold, Italic, Superscript, Subscript, Stack, Fraction, End };
static_assert(TextFormat::End < (TextFormat)'\t', "");

inline String regular(string s) { return char(TextFormat::Regular) + s + char(TextFormat::End); }
inline String bold(string s) { return char(TextFormat::Bold) + s + char(TextFormat::End); }
inline String italic(string s) { return char(TextFormat::Italic) + s + char(TextFormat::End); }
inline String superscript(string s) { return char(TextFormat::Superscript) + s + char(TextFormat::End); }
inline String subscript(string s) { return char(TextFormat::Subscript) + s + char(TextFormat::End); }
inline String stack(string s) { return char(TextFormat::Stack) + s + char(TextFormat::End); }
inline String fraction(string s) { return char(TextFormat::Fraction) + s + char(TextFormat::End); }

/// Text is a \a Widget displaying text (can be multiple lines)
struct Text : virtual Widget {
    /// Create a caption that display \a text using a \a size pt (points) font
	Text(const string text="", float size=16, bgr3f color=0, float opacity=1, float wrap=0, string font="DejaVuSans", bool hint=true, float interline=1, bool center=true, int2 minimalSizeHint=0, bool justifyExplicitLineBreak = false);

    // Parameters
    /// Displayed text in UTF32
    array<uint> text;
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
    /// Horizontal alignment
    bool center;

	bool justifyExplicitLineBreak;
    /// Minimal size hint
    vec2 minimalSizeHint;

	struct TextLayout layout(float wrap=0) const;
	vec2 sizeHint(vec2 size=0) override;
    shared<Graphics> graphics(vec2 size) override;
};
