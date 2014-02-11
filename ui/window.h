#pragma once
/// \file window.h X11 window display and input
#include "thread.h"
#include "widget.h"
#include "function.h"
#include "map.h"
#if !__arm__
#define X11 1
#endif
#if X11
union XEvent;
#else
struct PollDevice : Device, Poll {
    PollDevice(const string &path):Device(path,root(),Flags(ReadWrite|NonBlocking)),Poll(Device::fd){}
    void event() override { eventReceived(); }
    signal<> eventReceived;
};
#endif

/// Interfaces \a widget as a window on an X11 display server
#if X11
struct Window : Socket, Poll {
#else
struct Window : Device {
#endif
    /// Creates an initially hidden window for \a widget, use \a show to display
    /// \note size admits special values: 0 means fullscreen and negative \a size creates an expanding window)
    Window(Widget* widget, int2 size=int2(-1,-1), const string& name=""_, const Image& icon=Image());

    /// Shows window.
    void show();
    /// Hides window.
    void hide();

    /// Sets window title to \a title
    void setTitle(const string& title);
    /// Sets window icon to \a icon
    void setIcon(const Image& icon);

    /// Registers local shortcut on \a key
    signal<>& localShortcut(Key);
    /// Registers global shortcut on \a key
    signal<>& globalShortcut(Key);

    /// Sends a partial update
    void putImage(int2 position, int2 size);

    /// Display size
    int2 displaySize=0;
    /// Widget managed by this window
    Widget* widget;
    /// Whether this window is currently mapped. This doesn't imply the window is visible (can be covered)
    bool mapped = false;
    /// Geometry
    int2 position=0, size=0;
    /// Shortcuts triggered when a key is pressed
    map<uint, signal<>> shortcuts;
    /// Current widget that has the keyboard input focus
    Widget* focus=0;
    /// Current widget that has the drag focus
    Widget* drag=0;
    /// Current widget that gets keyboard events reported without modifiers (when in focus)
    Widget* directInput=0;
    /// Current cursor position
    int2 cursorPosition=0;
    /// Current cursor
    Cursor cursor = Cursor::Arrow;
    /// Background style
    bool oxygenBackground = true;

    /// Renders window background to \a target
    void renderBackground(Image& target);
    /// Gets current text selection
    /// \note The selection owner might lock this process if it fails to notify
    String getSelection(bool clipboard=false);
    /// Sets window cursor if cursor is inside region
    void setCursor(Rect region, Cursor cursor);

    /// Rendering target
    Image target;
    /// Drag state
    int2 dragStart, dragPosition, dragSize;
    /// Whether a render request was skipped while unmapped
    bool needUpdate = true;

#if X11
    /// Properly destroys X GC and Window
    ~Window();

    /// Event handler
    void event();
    /// Schedules window rendering after all events have been processed (i.e Poll::wait())
    void render();

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

    /// Moves window to \a position
    void setPosition(int2 position);
    /// Resizes window to \a size
    void setSize(int2 size);
    /// Moves window to \a position and resizes to \a size in one request
    void setGeometry(int2 position, int2 size);
    /// Sets window type to \a type
    void setType(const string& type);

    /// Returns cursor icon for \a cursor
    const Image& cursorIcon(Cursor cursor);
    /// Returns cursor hotspot for \a cursor
    int2 cursorHotspot(Cursor cursor);
    /// Sets window cursor
    void setCursor(Cursor cursor, uint window=0);

    /// Returns a snapshot of the root window
    Image getSnapshot();

    /// If set, this window will hide on leave events (e.g for dropdown menus)
    bool hideOnLeave = false;
    /// If set, this window will not be managed by the session window manager
    bool overrideRedirect;
    enum Anchor { Float, Left=1<<0, Right=1<<1, HCenter=Left|Right, Top=1<<2, Bottom=1<<3, VCenter=Top|Bottom,
                  Center=HCenter|VCenter, TopLeft=Top|Left, TopRight=Top|Right, BottomLeft=Bottom|Left, BottomRight=Bottom|Right };
    /// If set, this window will always be anchored to this position
    Anchor anchor = Float;

    /// Signals sent frames
    signal<> frameSent;

    /// Root window
    uint root = 0;
    /// Root visual
    uint visual=0;
    /// This window base resource id
    uint id = 0;

    /// Synchronizes access to connection and event queue
    Lock lock;

    /// KeyCode range
    uint minKeyCode=8, maxKeyCode=255;
    /// Associated window resource (relative to \a id)
    enum Resource { XWindow, GContext, Colormap, Segment, Pixmap, Picture, XCursor, SnapshotSegment };

    /// System V shared memory
    int shm = 0;
    /// Shared window buffer state
    enum { Idle, Server, Wait } state = Idle;

    /// bgra32 XRender PictFormat (for Cursor)
    uint format=0;

    uint16 sequence=-1;
    uint send(const ref<byte>& request);

    struct QEvent { default_move(QEvent); uint8 type; unique<XEvent> event; };
    array<QEvent> eventQueue;
    Semaphore semaphore;

    /// Reads an X reply (checks for errors and queue events)
    template<class T> T readReply(const ref<byte>& request);
#else
    /// Renders immediately current widget to framebuffer
    void render();
    /// Touchscreen event handler
    void touchscreenEvent();
    /// Buttons event handler
    void buttonEvent();

    uint stride=0, bytesPerPixel=0;
    Map framebuffer;
    PollDevice touchscreen {"/dev/input/event0"_};
    PollDevice buttons {"/dev/input/event4"_};
    int previousState = 0, state = 0;
#endif
};
