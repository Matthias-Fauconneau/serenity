#pragma once
#include "process.h"
#include "function.h"
#include "image.h"
#include "map.h"
#include "widget.h"

/// Display size
extern int2 display;

/// Window binds \a widget to an X window
struct Window : Poll {
    no_copy(Window)

    /// Creates an initially hidden window for \a widget, use \a show to display
    /// \note negative size is screen.size+size
    Window(Widget* widget, int2 size=int2(-1,-1),
           const ref<byte>& name=""_, const Image<byte4>& icon=Image<byte4>(), bool overrideRedirect=false);

    /// Event handler
    void event(const pollfd&);
    /// Reads one X event
    void readEvent(uint8 type);
    /// Reads an X reply while checking pending errors and processing queued events
    template<class T> T readReply();
    /// Returns Atom for \a name
    uint Atom(const ref<byte>& name);
    /// Returns name for \a atom
    string AtomName(uint atom);
    /// Returns KeySym for \a code
    uint KeySym(uint8 code);
    /// Returns property \a name on \a window
    template<class T> array<T> getProperty(uint window, const ref<byte>& name, uint size=2+128*128);

    /// Shows window. Connects to (i.e registerPoll) 'WM' local broadcast port and emits a new top level (id=0) window
    void show();
    /// Hides window. Emits a window hide and disconnect from 'WM" local broadcast (i.e unregisterPoll)
    void hide();
    /// Toggle visibility (e.g for popup menus)
    void toggle() { if(mapped) hide(); else show(); }

    /// Schedules window rendering after all events have been processed (i.e Poll::wait())
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

    /// Registers local shortcut on \a key
    signal<>& localShortcut(Key);
    /// Registers global shortcut on \a key
    signal<>& globalShortcut(Key);

    /// Gets current text selection
    /// \note The selection owner might lock this process if it fails to notify
    string getSelection();
    /// Sets window cursor
    void setCursor(Image<byte4>, uint window);

    /// Widget managed by this window
    Widget* widget;
    /// Whether this window is currently mapped. This doesn't imply the window is visible (can be covered)
    bool mapped = false;
    /// If set, this window will hide on leave events (e.g for dropdown menus)
    bool hideOnLeave = false;
    /// If set, \a Widget::update will be called before next \a render
    bool needUpdate = true;
    /// If set, this window will not be managed by the session window manager
    bool overrideRedirect;
    /// Window position and size
    int2 position, size;

    /// Socket to system local X display server
    const int x; /// \note Each window opens its own socket to simplify code (i.e no same process optimization)
    /// Root window
    uint root;
    /// This window base resource id
    uint id = 0;
    /// Associated window resource (relative to \a id)
    enum Resource { XWindow, GContext, Colormap, Segment, Pixmap, Cursor };
    /// MIT-SHM extension code

    /// System V shared memory
    int shm = 0;
    /// Shared window back buffer
    Image<byte4> buffer;
    /// Shared window buffer state
    enum { Idle, Server, Wait } state = Idle;

    /// Shortcuts triggered when a key is pressed
    map<uint16, signal<> > localShortcuts;
    map<uint16, signal<> > globalShortcuts;
};

/// Popup instantiates a widget in a small popup window
template<class T> struct Popup : T {
    Window window __(this, int2(256,256));
    Popup(T&& t=T()) : T(move(t)) { window.hideOnLeave=true; }
};
