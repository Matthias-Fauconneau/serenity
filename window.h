#pragma once
#include "process.h"
#include "function.h"
#include "image.h"
#include "map.h"
#include "widget.h"
#include "x.h"

/// Display size
extern int2 display;

enum Anchor { Float, Left=1<<0, Right=1<<1, HCenter=Left|Right, Top=1<<2, Bottom=1<<3, VCenter=Top|Bottom,
              Center=HCenter|VCenter, TopLeft=Top|Left, TopRight=Top|Right, BottomLeft=Bottom|Left, BottomRight=Bottom|Right };

/// Window binds \a widget to an X window
struct Window : Socket, Poll {
    no_copy(Window)

    /// Creates an initially hidden window for \a widget, use \a show to display
    /// \note size admits special values: 0 means fullscreen and negative \a size creates an expanding window)
    Window(Widget* widget, int2 size=int2(-1,-1), const ref<byte>& name=""_, const Image& icon=Image(),
           const ref<byte>& type="_NET_WM_WINDOW_TYPE_NORMAL"_);

    /// Event handler
    void event();
    /// Processes one X event
    void processEvent(uint8 type, const XEvent& e);
    /// Returns Atom for \a name
    uint Atom(const ref<byte>& name);
    /// Returns name for \a atom
    string AtomName(uint atom);
    /// Returns KeySym for \a code
    uint KeySym(uint8 code);
    /// Returns KeyCode for \a sym
    uint KeyCode(Key sym);
    /// Returns property \a name on \a window
    template<class T> array<T> getProperty(uint window, const ref<byte>& name, uint size=2+128*128);

    uint16 sequence=-1;
    void send(const ref<byte>& request);

    struct QEvent { uint8 type; XEvent event; } packed;
    array<QEvent> queue;
    /// Reads an X reply while checking pending errors and processing queued events
    template<class T> T readReply();

    /// Shows window.
    void show();
    /// Hides window.
    void hide();
    /// Toggle visibility (e.g for popup menus)
    void toggle() { if(mapped) hide(); else show(); }

    /// Schedules window rendering after all events have been processed (i.e Poll::wait())
    void render();

    /// Moves window to \a position
    void setPosition(int2 position);
    /// Resizes window to \a size
    void setSize(int2 size);
    /// Moves window to \a position and resizes to \a size in one request
    void setGeometry(int2 position, int2 size);
    /// Sets window title to \a title
    void setTitle(const ref<byte>& title);
    /// Sets window icon to \a icon
    void setIcon(const Image& icon);
    /// Sets window type to \a type
    void setType(const ref<byte>& type);

    /// Registers local shortcut on \a key
    signal<>& localShortcut(Key);
    /// Registers global shortcut on \a key
    signal<>& globalShortcut(Key);

    /// Gets current text selection
    /// \note The selection owner might lock this process if it fails to notify
    string getSelection();

    enum Cursor { Arrow, Horizontal, Vertical, FDiagonal, BDiagonal, Move, Cross } cursor=Cross;
    /// Returns cursor icon for \a cursor
    const Image& cursorIcon(Cursor cursor);
    /// Returns cursor hotspot for \a cursor
    int2 cursorHotspot(Window::Cursor cursor);
    /// Sets window cursor
    void setCursor(Cursor cursor, uint window=0);

    /// Widget managed by this window
    Widget* widget;
    /// Whether this window is currently mapped. This doesn't imply the window is visible (can be covered)
    bool mapped = false;
    /// If set, this window will hide on leave events (e.g for dropdown menus)
    bool hideOnLeave = false;
    /// If set, this window will not be managed by the session window manager
    bool overrideRedirect;
    /// If set, this window will resize to widget->sizeHint before any rendering
    bool autoResize = false;
    /// If set, this window will always be anchored to this position
    Anchor anchor = Float;
    /// Window position and size
    int2 position, size;
    /// Window background intensity and opacity
    int backgroundColor=0xE0,backgroundCenter=0xF0,backgroundOpacity=0xFF;

    /// Root window
    uint root = 0;
    /// KeyCode range
    uint minKeyCode=8, maxKeyCode=255;
    /// This window base resource id
    uint id = 0;
    /// Associated window resource (relative to \a id)
    enum Resource { XWindow, GContext, Colormap, Segment, Pixmap, Picture, XCursor };

    /// System V shared memory
    int shm = 0;
    /// Shared window back buffer
    Image buffer;
    /// Shared window buffer state
    enum { Idle, Server, Wait } state = Idle;

    /// bgra32 Render PictFormat
    uint format=0;

    /// Shortcuts triggered when a key is pressed
    map<uint16, signal<> > shortcuts;

    int2 dragStart, dragPosition, dragSize;
};
