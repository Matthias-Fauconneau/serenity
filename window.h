#pragma once
#include "interface.h"
#include "process.h"

#define None None
#define Font XID
#define Window XID
#include <X11/Xlib.h>
#undef Window
#undef Font
#include <X11/extensions/XShm.h>

#define Atom(name) XInternAtom(x, #name, 1)

/// Window embeds \a widget in an X11 window, displaying it and forwarding user input.
struct Window : Poll {
    no_copy(Window)
    /// Display \a widget in a window
    /// \note Make sure the referenced widget is initialized before running this constructor
    /// \note size admits special values (0: screen.size, -x: widget.sizeHint + margin=-x-1)
    Window(Widget* widget, int2 size=int2(0,0), const string& name=string(), const Image& icon=Image(), ubyte opacity=192);
    /// Update the window by handling any incoming events
    void update();
    /// Repaint window contents. Called by update after an event is accepted by a widget.
    void render();

    /// Show window
    void show();
    /// Hide window
    void hide();
    /// Set visibility
    void setVisible(bool visible);
    /// Current visibility
    bool visible = false;

    /// Move window to \a position
    void setPosition(int2 position);
    /// Resize window to \a size
    void setSize(int2 size);
    /// Toggle windowed/fullscreen mode
    void setFullscreen(bool fullscreen=true);
    /// Rename window to \a name
    void setName(const string& name);
    /// Set window icon to \a icon
    void setIcon(const Image& icon);
    /// Set window type (using Extended Window Manager Hints)
    void setType(const string& type);
    /// Set window override_redirect attribute
    void setOverrideRedirect(bool override_redirect);

    /// Register global shortcut named \a key (X11 KeySym)
    static uint addHotKey(const string& key);
    /// User pressed a key (including global hot keys)
    static signal<Key> keyPress;

    /// Set keyboard input focus
    void setFocus(Widget* focus);
    /// Current widget that has the keyboard input focus
    static Widget* focus;

    /// Get current text selection
    static string getSelection();

    /// Get X11 property \a name on \a window
    template<class T> static array<T> getProperty(XID window, const char* property);
protected:
    /// Set X11 property \a name to \a value
    template<class T> void setProperty(const char* type,const char* name, const array<T>& value);

    void event(pollfd);
    void event(const XEvent& e);

    static Display* x;
    static int2 screen;
    static int depth;
    static Visual* visual;
    static map<XID, Window*> windows;

    XID id;
    GC gc;
    XImage* image;
    XShmSegmentInfo shminfo;
    int2 position, size;
    Widget& widget;
    ubyte opacity;
};
