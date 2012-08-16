#pragma once
#include "vector.h"

/// Event type
enum Event { Error, Reply, KeyPress, KeyRelease, ButtonPress, ButtonRelease, Motion, Enter, Leave, FocusIn, FocusOut, KeymapNotify, Expose, GraphicsExpose, NoExpose, VisibilityNotify, CreateNotify, DestroyNotify, UnmapNotify, MapNotify, MapRequest, ReparentNotify, ConfigureNotify, ConfigureRequest, GravityNotify, ResizeRequest, CirculateNotify, CirculateRequest, PropertyNotify, SelectionClear, SelectionRequest, SelectionNotify, ColormapNotify , ClientMessage };
/// Button/Key codes
//#include <X11/keysym.h>
enum Key {
    None, LeftButton, MiddleButton, RightButton, WheelDown, WheelUp,
    Escape=0xff1b, Return=0xff0d, Delete=0xffff, BackSpace=0xff08,
    Home=0xff50, LeftArrow, UpArrow, RightArrow, DownArrow, End=0xff57,
    Email = 0x1008ff19, WWW=1008ff18 //TODO: Play
    //TODO: Touchbook buttons Extra,Power (XInput)
};

/// Widget is an abstract component to compose user interfaces
struct Widget {
    Widget(){}
    Widget(Widget&& o):position(o.position),size(o.size){}
    virtual ~Widget() {}
/// Layout
    int2 position; /// position of the widget within its parent widget
    int2 size; /// size of the widget
    /// Preferred size (positive means preferred, negative means expanding (i.e benefit from extra space))
    /// \note space is first allocated to preferred widgets, then to expanding widgets.
    virtual int2 sizeHint() { return int2(0,0); }
    /// Notify widgets to update any cache invalidated by \a position,\a size or property changes
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
