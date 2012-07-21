#pragma once
#include "process.h"
#include "signal.h"
#include "image.h"
#include "map.h"
#include "widget.h"

/// Window binds \a widget to display and input
struct Window : Poll {
    no_copy(Window)
    /// Creates a window for \a widget
    /// \note Windows are initially hidden, use \a show to display windows.
    /// \note size admits special values (0: screen.size, -x: widget.sizeHint + margin=-x-1), widget.sizeHint will be called from \a show
    Window(Widget* widget, const string &name=string(), const Image<byte4>& icon=Image<byte4>(), int2 size=int2(-1,-1));
    ~Window();
    /// Renders window
    void render();

    /// Shows window
    void show();
    /// Hides window
    void hide();
    /// Current visibility
    bool visible = false;

    /// Moves window to \a position
    void setPosition(int2 position);
    /// Resizes window to \a size
    void setSize(int2 size);
    /// Renames window to \a name
    void setName(const string& name);
    /// Sets window icon to \a icon
    void setIcon(const Image<byte4>& icon);
    /// Sets window type
    void setType(int type);

    /// Register local shortcut on \a key
    signal<>& localShortcut(Key);
    /// Register global shortcut on \a key
    signal<>& globalShortcut(Key);

    /// Returns if this window has keyboard input focus
    bool hasFocus();
    /// Set keyboard input focus
    void setFocus(Widget* focus);
    /// Current widget that has the keyboard input focus
    static Widget* focus;

    /// Get current text selection
    static string getSelection();

    /// Event handler
    void event(pollfd);

    ///  Input Devices
#if __arm__
    int touch,buttons;
#endif
    int keyboard,mouse;

    /// Virtual terminal (to switch from/to X)
    int vt;
    /// Connection to taskbar
    //int taskbar; TODO
    /// Shortcuts triggered when \a KeySym is pressed
    map<uint16, signal<> > shortcuts;

    Widget* widget;
};
