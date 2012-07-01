#pragma once
#include "vector.h"

/// Event type
enum Event { Motion, Press, Release, Enter, Leave };
/// Mouse button
enum Button { None, LeftButton, MiddleButton, RightButton, WheelDown, WheelUp };
/// Key code
enum Key {
    Escape=0xff1b, Return=0xff0d, Delete=0xffff, BackSpace=0xff08,
    Home=0xff50, Left, Up, Right, Down, End=0xff57
};

/// Widget is an abstract component to compose user interfaces
struct Widget {
    Widget()=default;
    Widget(Widget&&)=default;
    virtual ~Widget() {}
/// Layout
    int2 position; /// position of the widget within its parent widget
    int2 size; /// size of the widget
    /// Preferred size (positive means preferred, negative means expanding (i.e benefit from extra space))
    /// \note space is first allocated to preferred widgets, then to expanding widgets.
    virtual int2 sizeHint() { return int2(0,0); }
    /// Notify objects to process \a position,\a size or derived member changes
    virtual void update() {}

/// Paint
    /// Renders this widget.
    /// \a offset is the absolute position of the parent widget
    virtual void render(int2 /*parent*/) {};

/// Event
    /// Override \a mouseEvent to handle or forward user input
    /// \note \a mouseEvent is first called on the root \a Window::widget
    /// \return whether the mouse event should trigger a repaint
    virtual bool mouseEvent(int2 /*position*/, Event /*event*/, Button /*button*/) { return false; }
    /// Override \a keyPress to handle or forward user input
    /// \note \a keyPress is directly called on the current \a Window::focus
    /// \return whether the key press should trigger a repaint
    virtual bool keyPress(Key) { return false; }
    /// Override \a selectEvent to handle or forward user input
    /// \note \a selectEvent is called by \a Selection
    /// \return whether the select event should trigger a repaint
    virtual bool selectEvent() { return false; }
};

extern Widget* focus;
