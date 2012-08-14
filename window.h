#pragma once
#include "process.h"
#include "function.h"
#include "image.h"
#include "map.h"
#include "widget.h"

/// Window binds \a widget to an X window
struct Window : Poll {
    no_copy(Window)

    /// Creates an initially hidden window for \a widget, use \a show to display
    /// \note negative size is screen.size+size
    Window(Widget* widget, int2 size=int2(-1,-1), const ref<byte>& name=""_, const Image<byte4>& icon=Image<byte4>());

    /// Event handler
    void event(pollfd);
    /// Reads one X event
    void readEvent(uint8 type);
    /// Reads an X reply while checking pending errors and processing queued events
    template<class T> T readReply();
    /// Returns Atom for \a name
    uint Atom(const ref<byte>& name);
    /// Returns KeySym for \a code
    uint KeySym(uint8 code);

    /// Shows window. Connects to (i.e registerPoll) 'WM' local broadcast port and emits a new top level (id=0) window
    void show();
    /// Hides window. Emits a window hide and disconnect from 'WM" local broadcast (i.e unregisterPoll)
    void hide();
    /// Toggle visibility (e.g for popup menus)
    void toggle() { if(mapped) hide(); else show(); }

    /// Update layout hierarchy and repaint this window
    void update();
    /// Repaints this window
    /// \note use Poll::wait to schedule repaint after all events have been processed
    void render();

    /// Displays \a widget
    void setWidget(Widget* widget);
    /// Moves window to \a position
    void setPosition(int2 position);
    /// Resizes window to \a size
    void setSize(int2 size);
    /// Sets window title to \a title
    void setTitle(const ref<byte>& title);
    /// Sets window icon to \a icon
    void setIcon(const Image<byte4>& icon);

    /// Register local shortcut on \a key
    signal<>& localShortcut(Key);
    /// Register global shortcut on \a key
    signal<>& globalShortcut(Key);

    /// Get current text selection
    string getSelection();

    /// Widget managed by this window
    Widget* widget;
    /// Whether this window is currently mapped. This doesn't imply the window is visible (can be covered)
    bool mapped = false;
    /// Whether this window should close on leave (e.g for dropdown menus)
    bool hideOnLeave = false;

    /// Socket to system local X display server
    const int x; /// \note Each window opens its own socket to simplify code (i.e no same process optimization)
    /// X Window ID
    uint id;
    /// Root Window ID
    uint root;
    /// Graphic context
    uint gc;
    /// Display size
    int2 display;

    /// Shortcuts triggered when a key is pressed
    map<uint16, signal<> > localShortcuts;
    map<uint16, signal<> > globalShortcuts;
};

/// Popup instantiates a widget in a small popup window
template<class T> struct Popup : T {
    Window window i({this,int2(300,300)});
    Popup(T&& t=T()) : T(move(t)) { window.hideOnLeave=true; }
};
