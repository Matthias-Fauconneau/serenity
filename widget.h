#pragma once
#include "vector.h"
#include "string.h"

/// Event type
enum Event { Motion, Press, Release };
/// Key codes
enum i(class) Key {
    None, Escape, _1,_2,_3,_4,_5,_6,_7,_8,_9,_0, Minus, Equal, Backspace, Tab, Q,W,E,R,T,Y,U,I,O,P, LeftBrace, RightBrace, Enter, LeftCtrl,
    A,S,D,F,G,H,J,K,L, Semicolon, Apostrophe, Grave, LeftShift, BackSlash, Z,X,C,V,B,N,M, Comma, Dot, Slash, RightShift, KpAsterisk, LeftAlt,
    Space, Home=102, Up, PageUp, LeftArrow, RightArrow, End, Down, PageDown, Insert, Delete, Macro, Mute, VolumeDown, VolumeUp,
    Power=116, Left=0x110, Right, Middle, Side, Extra, WheelDown=0x150, WheelUp
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
    virtual void render(int2 /*parent*/)=0;

/// Event
    /// Override \a mouseEvent to handle or forward user input
    /// \note \a mouseEvent is first called on the root \a Window::widget
    /// \return whether the mouse event should trigger a repaint
    virtual bool mouseEvent(int2 /*position*/, Event /*event*/, Key /*button*/) { return false; }
    /// Override \a keyPress to handle or forward user input
    /// \note \a keyPress is directly called on the current \a Window::focus
    /// \return whether the key press should trigger a repaint
    virtual bool keyPress(Key) { return false; }
    /// Override \a selectEvent to handle or forward user input
    /// \note \a selectEvent is called by \a Selection when user changes selection (using press, wheel or arrows)
    /// \return whether the select event should trigger a repaint
    virtual bool selectEvent() { return false; }
};

extern Widget* focus;
