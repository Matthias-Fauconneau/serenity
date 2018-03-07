#pragma once
/// \file widget.h Widget interface to compose user interfaces
#include "render.h"
#include "input.h"

/// User interface colors
static constexpr bgr3f lightBlue {7./8, 3./4, 1./2};
static constexpr bgr3f gray {3./4, 3./4, 3./4};

inline String str(Key key) { return str(uint(key)); }

/// Abstract component to compose user interfaces
struct Widget {
 virtual ~Widget() {}
 // Contents
 virtual String title() const { return {}; }
 /// Preferred size (positive means preferred, negative means expanding (i.e benefit from extra space))
 /// \note space is first allocated to preferred widgets, then to expanding widgets.
 virtual vec2 sizeHint(vec2) { error("sizeHint"); return 0_0; }
 /// Renders graphic elements representing this widget to \a target.
 virtual void render(RenderTarget2D& target, vec2 offset=0_0, vec2 size=0_0) abstract;
 /// Returns stop position for scrolling
 /// \arg direction Direction of requested stop (-1: previous, 0: nearest, 1: next)
 /// \note Defaults to discrete uniform coarse stops
 virtual float stop(vec2 unused size, int unused axis, float currentPosition, int direction) { return currentPosition + direction * 64; }

 // Events
 /// Override \a mouseEvent to handle or forward user input
 /// \note \a mouseEvent is first called on the root Window#widget
 /// \return Whether the mouse event was accepted
 virtual bool mouseEvent(vec2 unused cursor, vec2 unused size, ::Event unused event, Button unused button, Widget*& unused focus) {
  return false;
 }
 /// Override \a keyPress to handle or forward user input
 /// \note \a keyPress is directly called on the current focus
 /// \return Whether the key press was accepted
 virtual bool keyPress(Key unused key, Modifiers unused modifiers) { return false; }
 virtual bool keyRelease(Key unused key, Modifiers unused modifiers) { return false; }
};

/// Returns whether this widget has the keyboard focus
bool hasFocus(Widget* widget);

/// Cursor icons
enum class MouseCursor { Arrow, Text };
/// Sets mouse cursor
void setCursor(MouseCursor cursor);
/// Returns last text selection (or if clipboard is true, last copy)
String getSelection(bool clipboard);
void setSelection(string selection, bool clipboard);
