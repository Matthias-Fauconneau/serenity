#pragma once
/// \file window.h X11 window display and input
#include "thread.h"
#include "function.h"
#include "image.h"
#include "map.h"
#include "widget.h"
#include "x.h"

enum Anchor { Float, Left=1<<0, Right=1<<1, HCenter=Left|Right, Top=1<<2, Bottom=1<<3, VCenter=Top|Bottom,
              Center=HCenter|VCenter, TopLeft=Top|Left, TopRight=Top|Right, BottomLeft=Bottom|Left, BottomRight=Bottom|Right };

extern int2 displaySize;

/// Interfaces \a widget as a window on an X11 display server
struct Window : Socket, Poll {
    enum Renderer { Raster, OpenGL };
    /// Creates an initially hidden window for \a widget, use \a show to display
    /// \note size admits special values: 0 means fullscreen and negative \a size creates an expanding window)
    Window(Widget* widget, int2 size=int2(-1,-1), const string& name=""_, const Image& icon=Image(),
           const string& type="_NET_WM_WINDOW_TYPE_NORMAL"_,Thread& thread=mainThread, Renderer renderer=Raster);
    /// Creates an initially hidden window for \a widget, use \a show to display
    /// \note size admits special values: 0 means fullscreen and negative \a size creates an expanding window)
    Window(Widget* widget, int2 size, const string& name,  const Image& icon, Renderer renderer) :
        Window(widget, size, name, icon, "_NET_WM_WINDOW_TYPE_NORMAL"_,mainThread, renderer){}
    ~Window() { destroy(); }

    /// Creates window.
    void create();
    /// Destroys window.
    void destroy();

    /// Schedules window rendering after all events have been processed (i.e Poll::wait())
    void render();

    /// Event handler
    void event();

    /// Processes one X event
    void processEvent(uint8 type, const XEvent& e);
    /// Returns Atom for \a name
    uint Atom(const string& name);
    /// Returns KeySym for key \a code and modifier \a state
    Key KeySym(uint8 code, uint8 state);
    /// Returns KeyCode for \a sym
    uint KeyCode(Key sym);
    /// Returns property \a name on \a window
    template<class T> array<T> getProperty(uint window, const string& name, uint size=2+128*128);

    /// Shows window.
    void show();
    /// Hides window.
    void hide();
    /// Toggle visibility (e.g for popup menus)
    void toggle() { if(mapped) hide(); else show(); }

    /// Moves window to \a position
    void setPosition(int2 position);
    /// Resizes window to \a size
    void setSize(int2 size);
    /// Moves window to \a position and resizes to \a size in one request
    void setGeometry(int2 position, int2 size);
    /// Sets window title to \a title
    void setTitle(const string& title);
    /// Sets window icon to \a icon
    void setIcon(const Image& icon);
    /// Sets window type to \a type
    void setType(const string& type);

    /// Registers local shortcut on \a key
    signal<>& localShortcut(Key);
    /// Registers global shortcut on \a key
    signal<>& globalShortcut(Key);

    /// Gets current text selection
    /// \note The selection owner might lock this process if it fails to notify
    String getSelection(bool clipboard=false);

    /// Returns cursor icon for \a cursor
    const Image& cursorIcon(Cursor cursor);
    /// Returns cursor hotspot for \a cursor
    int2 cursorHotspot(Cursor cursor);
    /// Sets window cursor
    void setCursor(Cursor cursor, uint window=0);
    /// Sets window cursor if cursor is inside region
    void setCursor(Rect region, Cursor cursor);

    /// Returns a snapshot of the root window
    Image getSnapshot();

    /// Widget managed by this window
    Widget* widget;
     /// Whether this window is currently existing.
    bool created = false;
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
    int2 position=0, size=0;

    /// Selects between software or OpenGL rendering
    Renderer renderer;
    /// Window background intensity and opacity
    float backgroundColor=14./16, backgroundCenter=15./16, backgroundOpacity=1;
    bool clearBackground = true, featherBorder = false;

    /// Shortcuts triggered when a key is pressed
    map<uint16, signal<>> shortcuts;
    /// Current widget that has the keyboard input focus
    Widget* focus=0;
    /// Current widget that has the drag focus
    Widget* drag=0;

    /// Current cursor
    Cursor cursor = Cursor::Arrow;
    /// Current cursor position
    int2 cursorPosition;
    /// Window drag state
    int2 dragStart, dragPosition, dragSize;

    /// Signals when a render request completed
    signal<> frameReady;

    /// Root window
    uint root = 0;
    /// Root visual
    uint visual=0;
    /// This window base resource id
    uint id = 0;

    /// Synchronize reading from event and readReply
    Lock readLock;

    /// KeyCode range
    uint minKeyCode=8, maxKeyCode=255;
    /// Associated window resource (relative to \a id)
    enum Resource { XWindow, GContext, Colormap, Segment, Pixmap, Picture, XCursor, SnapshotSegment };

    /// System V shared memory
    int shm = 0;
    /// Shared window back buffer
    Image buffer;
    /// Shared window buffer state
    enum { Idle, Server, Wait } state = Idle;

    /// bgra32 XRender PictFormat (for Cursor)
    uint format=0;

    uint16 sequence=-1;
    void send(const ref<byte>& request);

    struct QEvent { uint8 type; XEvent event; };
    array<QEvent> eventQueue;
    /// Reads an X reply (checks for errors and queue events)
    template<class T> T readReply(const ref<byte>& request);
};
