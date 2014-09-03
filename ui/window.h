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
    /// Whether this window is currently mapped. This doesn't imply the window is visible (can be covered)
    bool mapped = false;
    /// Window size
    int2 size=0;
    /// Updates to be rendered
    struct Update { Graphics graphics; int2 origin, size; };
    array<Update> updates;
    /// Rendering target in shared memory
    Image target;
    /// Background style
    enum Background { NoBackground, Black, White, Oxygen } background = Oxygen;
    /// Associated window resource (relative to \a id)
    enum Resource { XWindow, GraphicContext, Colormap, Segment, Pixmap, PresentEvent };
    /// System V shared memory
    uint shm = 0;
    /// Shared window buffer state
    enum State { Idle, Copy, Present } state = Idle;

// Control
    /// Actions triggered when a key is pressed
    map<Key, function<void()>> actions;
    /// Current widget that has the keyboard input focus
    Widget* focus = widget;
    /// Current widget that has the drag focus
    Widget* drag = 0;

// Methods
    /// Creates an initially hidden window for \a widget, use \a show to display
    /// \note size admits special values: 0 means fullscreen and negative \a size creates an expanding window)
    Window(Widget* widget, int2 size=int2(-1,-1), const string& name=""_, const Image& icon=Image());
    /// Frees the graphics context and destroys the window
    virtual ~Window();

// Connection
    /// Processes an event
    void processEvent(const ref<byte>& ge);

 // Window
    /// Shows window.
    void show();
    /// Hides window.
    void hide();

    /// Sets window title to \a title
    void setTitle(const string& title);
    /// Sets window icon to \a icon
    void setIcon(const Image& icon);

// Display
    /// Resizes Shm pixmap
    void setSize(int2 size);
    /// Schedules partial rendering after all events have been processed (\sa Poll::queue)
    void render(Graphics&& graphics, int2 origin, int2 size);
    /// Schedules window rendering after all events have been processed (\sa Poll::queue)
    void render();
    /// Event handler
    void event() override;
};
