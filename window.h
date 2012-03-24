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
    /// Initialize an X11 window for \a widget
    /// \note Windows are initially hidden, use \a show to display windows.
    /// \note size admits special values (0: screen.size, -x: widget.sizeHint + margin=-x-1), widget.sizeHint will be called from \a show.
    Window(Widget* widget, const string &name=string(), const Image &icon=Image(), int2 size=int2(-1,-1), ubyte opacity=192);
    /// Create the window
    void create();
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
    void setPosition(int2 position);
    /// Resize window to \a size
    void setSize(int2 size);
    /// Toggle windowed/fullscreen mode
    void setFullscreen(bool fullscreen=true);
    /// Rename window to \a title
    void setTitle(const string& title);
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
    static void sync();
protected:
    /// Set X11 property \a name to \a value
    template<class T> void setProperty(const char* type,const char* name, const array<T>& value);

    void event(pollfd);
    bool event(const XEvent& e);

    static Display* x;
    static int2 screen;
    static int depth;
    static Visual* visual;
    static map<XID, Window*> windows;

    XID id=0;

    int2 position, size;
    string title;
    Image icon;

    GC gc;
    XImage* image;
    XShmSegmentInfo shminfo;
public:
    int bgCenter=240,bgOuter=224;
    Widget& widget;
    ubyte opacity;
};
