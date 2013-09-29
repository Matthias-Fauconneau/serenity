#pragma once
/// \file widget.h Widget interface to compose user interfaces
#include "vector.h"
#include "rect.h"

/// Key symbols
enum Key {
    Escape=0xff1b, BackSpace=0xff08, Tab, Return=0xff0d, Home=0xff50, LeftArrow, UpArrow, RightArrow, DownArrow, End=0xff57, PrintScreen=0xff61,
    Execute, Insert,
    KP_Enter=0xff8d, KP_Multiply=0xffaa, KP_Add, KP_Separator, KP_Sub, KP_Decimal, KP_Divide, KP_0,KP_1,KP_2,KP_3,KP_4,KP_5,KP_6,KP_7,KP_8,KP_9,
    ShiftKey=0xffe1, ControlKey=0xffe3,
    Delete=0xffff,
    Play=0x1008ff14, WWW=0x1008ff18, Email=0x1008ff19, Power=0x1008ff2a

};
enum Modifiers { NoModifiers=0, Shift=1<<0, Control=1<<2, Alt=1<<3, NumLock=1<<4/*, Meta=1<<6*/ };

/// Abstract component to compose user interfaces
struct Widget {
// Layout
    /// Preferred size (positive means preferred, negative means expanding (i.e benefit from extra space))
    /// \note space is first allocated to preferred widgets, then to expanding widgets.
    virtual int2 sizeHint() { return -1; }
    /// Renders this widget.
    virtual void render(int2 position, int2 size)=0;
    /// Renders this widget.
    /// \arg rect is the absolute region for the widget
    void render(Rect rect) { render(rect.position(),rect.size()); }

// Event
    /// Mouse event type
    enum Event { Press, Release, Motion, Enter, Leave };
    /// Mouse buttons
    enum Button { None, LeftButton, MiddleButton, RightButton, WheelUp, WheelDown };
    /// Override \a mouseEvent to handle or forward user input
    /// \note \a mouseEvent is first called on the root Window#widget
    /// \return Whether the mouse event was accepted
    virtual bool mouseEvent(int2 cursor, int2 size, Event event, Button button) { (void)cursor, (void)size, (void)event, (void)button; return false; }
    /// Convenience overload for layout implementation
    bool mouseEvent(Rect rect, int2 cursor, Event event, Button button) { return mouseEvent(cursor-rect.min,rect.size(),event,button); }
    /// Override \a keyPress to handle or forward user input
    /// \note \a keyPress is directly called on the current focus
    /// \return Whether the key press was accepted
    virtual bool keyPress(Key key, Modifiers modifiers) { (void)key, (void) modifiers; return false; }
    virtual bool keyRelease(Key key, Modifiers modifiers) { (void)key, (void) modifiers; return false; }
};

// Accessors to the window which received the current event (may only be called from event methods)
/// Reports keyboard events to this widget
void setFocus(Widget* widget);
/// Returns whether this widget has the keyboard focus
bool hasFocus(Widget* widget);
/// Reports further mouse motion events only to this widget (until mouse button is released)
void setDrag(Widget* widget);
/// Returns last text selection (or if clipboard is true, last copy)
String getSelection(bool clipboard=false);
/// Cursor icons
enum class Cursor { Arrow, Horizontal, Vertical, FDiagonal, BDiagonal, Move, Text };
/// Sets cursor to be shown when mouse is in the given rectangle
void setCursor(Rect region, Cursor cursor);
