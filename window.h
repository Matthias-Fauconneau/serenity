#pragma once
#include "process.h"
#include "signal.h"
#include "image.h"
#include "map.h"
#include "widget.h"

/// Window binds \a widget to display and input
struct Window : Poll {
    no_copy(Window)
    /// Creates an initially hidden window for \a widget, use \a show to display
    /// \note size admits special values (0: screen.size, -x: widget.sizeHint + margin=-x-1), widget.sizeHint will be called from \a show
    Window(Widget* widget, int2 size=int2(-1,-1), string&& name=string(), Image<byte4>&& icon=Image<byte4>());
    Window(Window&& window)=default; //allow = Window() initialization
    ~Window();

    /// Shows window. Connects to (i.e registerPoll) 'WM' local broadcast port and emits a new top level (id=0) window
    void show();
    /// Hides window. Emits a window hide and disconnect from 'WM" local broadcast (i.e unregisterPoll)
    void hide();
    /// Toggle visibility (e.g for popup menus)
    void toggle() { if(shown) hide(); else show(); }

    /// Updates active and visible states. Emits cursor if leaving. Called on window state changes and cursor moves
    void update();
    /// Repaints this window
    void render();
    /// Emit current state
    void emit(bool emitTitle=false, bool emitIcon=false);

    /// Moves window to \a position
    void setPosition(int2 position);
    /// Resizes window to \a size
    void setSize(int2 size);
    /// Sets window title to \a title
    void setTitle(string&& title);
    /// Sets window icon to \a icon
    void setIcon(Image<byte4>&& icon);

    /// Register local shortcut on \a key
    signal<>& localShortcut(Key);
    /// Register global shortcut on \a key
    signal<>& globalShortcut(Key);

    /// Get current text selection
    static string getSelection();

    /// Event handler
    void event(pollfd);

    /// Widget managed by this window
    Widget* widget;
    /// Title of this window
    string title;
    /// Icon of this window
    Image<byte4> icon;
    /// Whether this window is currently shown (i.e emit/poll WM). This doesn't imply the window is visible (can be covered)
    bool shown = false;
    /// Whether this window should close on leave (e.g for dropdown menus)
    bool hideOnLeave = false;
    /// Whether this window should keep polling keyboard/buttons to handle global shortcuts
    bool globalShortcuts = false;

    /// Socket to system local cooperative window management broadcast port
    int wm=0;
    /// id of this window (default to new top level window)
    //uint id=-2;
    uint id=0;
    /// id of currently active window
    uint active=-1;
    /// Window state emitted for cooperative window management
    struct window {
        uint length; /// length of this message in bytes
        //uint id; /// also Z-ordering (0=bottom). A new window use -1 and is implicitly assigned the minimum available id
        uint id; /// also Z-ordering (0=top). A new window use 0 and shift all other windows
        int2 cursor; /// cursor position according to this window (the top containing one is right)
        int2 position;
        int2 size; /// 0: hide
        /// size>32: uint8 titleLength; char[titleLength] title; /// ""= system tray icon
        /// size>33+titleLength: uint16 width=16,height=16; byte4[width][height] icon; "" && 0x0 = not shown in taskbar
    };
    /// state of all windows for input/clip/cursor handling.
    array<window> windows;
    /// last known cursor position
    int2 cursor;

    ///  Input Devices
    int touch=0,buttons=0,keyboard=0,mouse=0;
    /// Shortcuts triggered when \a KeySym is pressed
    map<uint16, signal<> > shortcuts;
    /// Virtual terminal (to switch from/to X)
    int vt=0;
};

/// Popup instantiates a widget in a small popup window
template<class T> struct Popup : T {
    Window window{this,""_,Image<byte4>(),int2(300,300)};
    Popup(T&& t=T()) : T(move(t)) { window.hideOnLeave=true; }
};
