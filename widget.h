#pragma once
/// \file widget.h Widget interface to compose user interfaces
#include "vector.h"

enum MouseButton { None, LeftButton, MiddleButton, RightButton, WheelDown, WheelUp };
enum Key {
    Escape=0xff1b, BackSpace=0xff08, Return=0xff0d, Home=0xff50, LeftArrow, UpArrow, RightArrow, DownArrow, End=0xff57, PrintScreen=0xff61, Delete=0xffff,
    Play=0x1008ff14, Email=0x1008ff19, WWW=0x1008ff18
};

/// Abstract component to compose user interfaces
struct Widget {
// Layout
    /// Preferred size (positive means preferred, negative means expanding (i.e benefit from extra space))
    /// \note space is first allocated to preferred widgets, then to expanding widgets.
    virtual int2 sizeHint() { return 0; }
    /// Renders this widget.
    virtual void render(int2 position, int2 size)=0;
    /// Renders this widget.
    /// \arg rect is the absolute region for the widget
    void render(Rect rect) { render(rect.position(),rect.size()); }

// Event
    /// Mouse event type
    enum Event { Press, Release, Motion, Enter, Leave };
    /// Override \a mouseEvent to handle or forward user input
    /// \note \a mouseEvent is first called on the root Window#widget
    /// \return Whether the mouse event was accepted
    virtual bool mouseEvent(int2 unused cursor, int2 unused size, Event unused event, MouseButton unused button) { return false; }
    bool mouseEvent(Rect rect, int2 cursor, Event event, MouseButton button) { return mouseEvent(cursor-rect.min,rect.size(),event,button); }
    /// Override \a keyPress to handle or forward user input
    /// \note \a keyPress is directly called on the current focus
    /// \return Whether the key press was accepted
    virtual bool keyPress(Key) { return false; }
};

/// Current widget that has the keyboard input focus
extern Widget* focus;
/// Current widget that has the drag focus
extern Widget* drag;
/// Returns last text selection (middle-click paste)
string getSelection();
