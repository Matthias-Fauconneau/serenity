#pragma once
/// \file window.h Window display and input
#include "display.h"
#include "widget.h"
struct XEvent;

/// Interfaces \a widget as a window on a display server
struct Window : Display /*should reference but inherits for convenience*/ {
    /// Widget managed by this window
    Widget* widget;

// Display
    /// Window size
    union {
        struct { uint width, height; };
        int2 size = 0;
    };
    /// Window title
    String title;
	function<String()> getTitle;

    /// Associated window resource (relative to \a id)
    enum Resource { XWindow, GraphicContext, Colormap, Segment, Pixmap, PresentEvent };

    /// System V shared memory
    uint shm = 0;
    /// Rendering target in shared memory
    Image target;
    /// Shared window buffer state
    enum State { Idle, Copy, Present } state = Idle;

    /// Updates to be rendered
    struct Update { Graphics graphics; int2 origin, size; };
    array<Update> updates;

    /// Whether this window is currently mapped. This doesn't imply the window is visible (can be covered)
    bool mapped = false;

    /// Background style
    enum Background { NoBackground, Black, White, Oxygen } background = Oxygen;

// Control
    /// An event held to implement motion compression and ignore autorepeats
    unique<XEvent> heldEvent;
    /// Actions triggered when a key is pressed
    map<Key, function<void()>> actions;
    /// Current widget that has the keyboard input focus
    Widget* focus = widget;
    /// Current widget that has the drag focus
    Widget* drag = 0;

// Methods
    /// Creates an initially hidden window for \a widget, use \a show to display
    /// \note size admits special values: 0 means fullscreen and negative \a size creates an expanding window)
	Window(Widget* widget, int2 size=int2(-1,-1), function<String()> title={}, const Image& icon=Image());
    /// Frees the graphics context and destroys the window
    virtual ~Window();

// Connection
    /// Processes or holds an event
    void onEvent(const ref<byte> ge);
    /// Processes an event
    bool processEvent(const XEvent& ge);

 // Window
    /// Shows window.
    void show();
    /// Hides window.
    void hide();

    /// Sets window title to \a title
    void setTitle(const string title);
    /// Sets window icon to \a icon
    void setIcon(const Image& icon);

// Display
    /// Schedules partial rendering after all events have been processed (\sa Poll::queue)
    void render(Graphics&& graphics, int2 origin, int2 size);
    /// Schedules window rendering after all events have been processed (\sa Poll::queue)
    void render();
    /// Event handler
    void event() override;
};
