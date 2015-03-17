#pragma once
/// \file text.h Rich text label/paragraph widget
#include "widget.h"
#include "function.h"
#include "core/image.h"
#include "utf8.h"

/// Rich text format control code encoded in 00-1F range
// \note first word (until ' ') after a Link tag is not displayed but used as \a linkActivated identifier.
enum class TextFormat { Regular, Bold, Italic, Superscript, Subscript, Stack, Fraction, End };
static_assert(TextFormat::End < (TextFormat)'\t', "");

inline String regular(string s) { return char(TextFormat::Regular) + s + char(TextFormat::End); }
inline String bold(string s) { return char(TextFormat::Bold) + s + char(TextFormat::End); }
inline String italic(string s) { return char(TextFormat::Italic) + s + char(TextFormat::End); }
inline String superscript(string s) { return char(TextFormat::Superscript) + s + char(TextFormat::End); }
inline String subscript(string s) { return char(TextFormat::Subscript) + s + char(TextFormat::End); }
inline String stack(string s) { return char(TextFormat::Stack) + s + char(TextFormat::End); }
inline String fraction(string s) { return char(TextFormat::Fraction) + s + char(TextFormat::End); }

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

/// Text is a \a Widget displaying text (can be multiple lines)
struct Text : virtual Widget {
    /// Create a caption that display \a text using a \a size pixel font
    Text(const string text="", float size=16, bgr3f color=0, float opacity=1, float wrap=0, string font="DejaVuSans", bool hint=true,
	 float interline=1, int align=0, int2 minimalSizeHint=0, bool justify = false, bool justifyExplicitLineBreak = false);

	/// Displayed text in UCS4
	array<uint> text;
	// Parameters (all constants so any cached lastTextLayout cannot be invalidated by a parameter change)
    /// Font size
	const int size;
    /// Text color
	const bgr3f color;
    /// Text opacity
	const float opacity;
    /// Line wrap limit in pixels (0: no wrap)
	const float wrap = 0;
    /// Font name
	const string font;
    /// Whether font should be hinted for display
	const bool hint;
    /// Interline stretch
	const float interline;
    /// Horizontal alignment
	const int align;
	/// Whether to justify
	const bool justify;
	/// Whether to justify explicit line breaks
	const bool justifyExplicitLineBreak;
    /// Minimal size hint
	const vec2 minimalSizeHint;

	/// Caches last text layout (for a given wrap)
	TextLayout lastTextLayout;

	const TextLayout& layout(float wrap=0);
    vec2 sizeHint(vec2 size=0) override;
    shared<Graphics> graphics(vec2 size) override;
};
