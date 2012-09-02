#pragma once
#include "vector.h"
#include "display.h"

/// Button/Key codes
enum Button { None, LeftButton, MiddleButton, RightButton, WheelDown, WheelUp };
enum Key {
    Escape=0xff1b, Return=0xff0d, Delete=0xffff, BackSpace=0xff08,
    Home=0xff50, LeftArrow, UpArrow, RightArrow, DownArrow, End=0xff57,
    Email = 0x1008ff19, WWW=0x1008ff18 //TODO: Play
    //TODO: Touchbook buttons Extra,Power (XInput)
};

/// Widget is an abstract component to compose user interfaces
struct Widget {
    Widget(){}Widget(Widget&&){}
// Layout
    /// Preferred size (positive means preferred, negative means expanding (i.e benefit from extra space))
    /// \note space is first allocated to preferred widgets, then to expanding widgets.
    virtual int2 sizeHint() { return int2(0,0); }
    /// Renders this widget.
    /// \arg rect is the absolute region for the widget
    virtual void render(int2 position, int2 size)=0;
    void render(Rect rect) { render(rect.position(),rect.size()); }

// Event
    /// Mouse event type
    enum Event { Press, Release, Motion, Enter, Leave };
    /// Override \a mouseEvent to handle or forward user input
    /// \note \a mouseEvent is first called on the root \a Window::widget
    /// \return Whether the mouse event was accepted
    virtual bool mouseEvent(int2 unused cursor, int2 unused size, Event unused event, Button unused button) { return false; }
    bool mouseEvent(Rect rect, int2 cursor, Event event, Button button) { return mouseEvent(cursor-rect.min,rect.size(),event,button); }
    /// Override \a keyPress to handle or forward user input
    /// \note \a keyPress is directly called on the current \a Window::focus
    /// \return Whether the key press was accepted
    virtual bool keyPress(Key) { return false; }
    /// Override \a selectEvent to handle or forward user input
    /// \note \a selectEvent is called by \a Selection when user changes selection (using press, wheel or arrows)
    /// \return Whether the select event was accepted
    virtual bool selectEvent() { return false; }
};

/// Current widget that has the keyboard input focus
extern Widget* focus;
/// Current widget that has the drag focus
extern Widget* drag;
