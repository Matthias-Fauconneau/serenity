#pragma once
/// \file widget.h Widget interface to compose user interfaces
#include "graphics.h"
#include "input.h"

/// User interface colors
static constexpr bgr3f lightBlue (7./8, 3./4, 1./2);
static constexpr bgr3f gray (3./4, 3./4, 3./4);

inline String str(Key key) { return str(uint(key)); }

/// Abstract component to compose user interfaces
struct Widget {
    virtual ~Widget() {}
// Contents
    virtual String title() { return {}; }
    /// Preferred size (positive means preferred, negative means expanding (i.e benefit from extra space))
    /// \note space is first allocated to preferred widgets, then to expanding widgets.
    virtual vec2 sizeHint(vec2) = 0;
    /// Returns graphic elements representing this widget at the given \a size.
    virtual shared<Graphics> graphics(vec2 unused size) { error("Unimplemented, use graphics(size, clip)"); }
    virtual shared<Graphics> graphics(vec2 size, Rect unused clip) { return this->graphics(size); }
	/// Returns stop position for scrolling
	/// \arg direction Direction of requested stop (-1: previous, 0: nearest, 1: next)
	/// \note Defaults to discrete uniform coarse stops
    virtual float stop(vec2 unused size, int unused axis, float currentPosition, int direction=0) { return currentPosition + direction * 64; }

// Events
    /// Override \a mouseEvent to handle or forward user input
    /// \note \a mouseEvent is first called on the root Window#widget
    /// \return Whether the mouse event was accepted
    virtual bool mouseEvent(vec2 cursor, vec2 size, ::Event event, Button button, Widget*& focus) {
        (void)cursor, (void)size, (void)event, (void)button; (void)focus; return false;
    }
    /// Override \a keyPress to handle or forward user input
    /// \note \a keyPress is directly called on the current focus
    /// \return Whether the key press was accepted
    virtual bool keyPress(Key key, Modifiers modifiers) { (void)key, (void) modifiers; return false; }
    virtual bool keyRelease(Key key, Modifiers modifiers) { (void)key, (void) modifiers; return false; }
};

/// Returns whether this widget has the keyboard focus
bool hasFocus(Widget* widget);

/// Cursor icons
enum class Cursor { Arrow, Text };
/// Sets mouse cursor
void setCursor(Cursor cursor);
/// Returns last text selection (or if clipboard is true, last copy)
String getSelection(bool clipboard);
