#pragma once
#include "interface.h"
#include "process.h"

/// Window embeds \a widget in an X11 window, displaying it and forwarding user input.
//TODO: wayland
//forward declarations from Xlib.h
typedef struct _XDisplay Display;
typedef struct __GLXcontextRec* GLXContext;
typedef unsigned long XWindow;
struct Window : Poll {
    no_copy(Window)
    /// Display \a widget in a window
    /// \a name is the application class name (WM_CLASS)
    /// \note a Window must be created before OpenGL can be used (e.g most Widget constructors need a GL context)
    /// \note Make sure the referenced widget is initialized before running this constructor
    /// \note Currently only one window per process can be created (or GL context issues will occur)
    Window(Widget& widget, int2 size, const string& name);
    /// Update the window by handling any incoming events
    void update();
    /// Repaint window contents. Called by update after an event is accepted by a widget.
    void render();

    /// Show window
    void show();
    /// Hide window
    void hide();
    /// Current visibility
    bool visible = false;

    /// Move window to \a position
    void move(int2 position);
    /// Resize window to \a size
    void resize(int2 size);
    /// Toggle windowed/fullscreen mode
    void setFullscreen(bool fullscreen=true);
    /// Rename window to \a name
    void rename(const string& name);
    /// Set window icon to \a icon
    void setIcon(const Image& icon);
    /// Set window type (using Extended Window Manager Hints)
    void setType(const string& type);
    /// Set window override_redirect attribute
    void setOverrideRedirect(bool override_redirect);

    /// Register global shortcut named \a key (X11 KeySym)
    uint addHotKey(const string& key);
    /// User pressed a key (including global hot keys)
    signal<Key> keyPress;

    /// Set keyboard input focus
    void setFocus(Widget* focus);
    /// Current widget that has the keyboard input focus
    static Widget* focus;
protected:
    /// Set X11 property \a name to \a value
    /// \note The template is instantiated for strings (STRING,UTF8_STRING) and uints (ATOM)
    template<class T> void setProperty(const char* type,const char* name, const array<T>& value);

    pollfd poll();
    void event(pollfd);

    XWindow id;
    Display* x;
    static GLXContext ctx;
    Widget& widget;
};
