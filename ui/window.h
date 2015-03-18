#pragma once
/// \file window.h Window display and input
#include "display.h"
#include "widget.h"
#include "function.h"
#include "map.h"
namespace X11 { struct Event; }

struct Window : Poll {
	/// Widget managed by this window
	Widget* widget;

	// Display
	/// Window size
	int2 size = 0;
	/// Background color
	bgr3f backgroundColor = white;
	/// Current cursor
	Cursor cursor = Cursor::Arrow;

	/// Updates to be rendered
	struct Update { shared<Graphics> graphics; int2 origin, size; };
	Lock lock;
	array<Update> updates;

	// Input
	/// Actions triggered when a key is pressed
	map<Key, function<void()>> actions;
	/// Current widget that has the keyboard input focus
	Widget* focus = widget;
	/// Current widget that has the drag focus
	Widget* drag = 0;

	Window(Widget* widget, Thread& thread, int2 size = 0) : Poll(0,0,thread), widget(widget), size(size) {}
	virtual ~Window() {}

	// Display
	/// Schedules partial rendering after all events have been processed (\sa Poll::queue)
	void render(shared<Graphics>&& graphics, int2 origin, int2 size);
	/// Schedules window rendering after all events have been processed (\sa Poll::queue)
	void render();
	/// Immediately renders the first pending update to target
	Update render(const Image& target);

	virtual void setCursor(Cursor cursor) abstract;
	virtual String getSelection(bool clipboard) abstract;
};

/// Interfaces \a widget as a window on a display server
struct XWindow : Window, XDisplay /*should reference but inherits for convenience*/ {
	 /// Window title
    String title;
    function<String()> getTitle;
    /// Background color
    bgr3f backgroundColor = white;

    /// Associated window resource (relative to resource ID base Display::id)
	enum Resource { Window, GraphicContext, Colormap, PresentEvent, Segment, Pixmap, Picture, Cursor, CursorPixmap };
    /// System V shared memory
    uint shm = 0;
    /// Rendering target in shared memory
    Image target;
    /// Shared window buffer state
    enum State { Idle, Copy, Present } state = Idle;

    /// Whether this window is currently mapped. This doesn't imply the window is visible (can be covered)
    bool mapped = false;

    uint64 firstFrameCounterValue = 0;
	uint64 currentFrameCounterValue = 0;

    /// An event held to implement motion compression and ignore autorepeats
    //unique<XEvent> heldEvent;

	/// bgra32 XRender PictFormat (for Cursor)
	uint format=0;

    /// Creates an initially hidden window for \a widget, use \a show to display
    /// \note size admits special values: 0 means fullscreen and negative \a size creates an expanding window)
	XWindow(Widget* widget, Thread& thread, int2 size);
    /// Frees the graphics context and destroys the window
	~XWindow();

    // Connection
    /// Processes or holds an event
    void onEvent(const ref<byte> ge);
    /// Processes an event
	bool processEvent(const X11::Event& ge);

    // Window
    /// Shows window.
    void show();
    /// Hides window.
	void hide();

    /// Sets window title to \a title
    void setTitle(const string title);
    /// Sets window icon to \a icon
    void setIcon(const Image& icon);
    /// Resizes window to \a size
    void setSize(int2 size);

	// Display
    void event() override;
	void setCursor(::Cursor cursor) override;

	// IPC
	/// Gets current text selection
	/// \note The selection owner might lock this process if it fails to notify
	String getSelection(bool clipboard) override;
};

struct DRMWindow : Window {
	static unique<Display> display;

	/// Creates an initially hidden window for \a widget, use \a show to display
	/// \note size admits special values: 0 means fullscreen and negative \a size creates an expanding window)
	DRMWindow(Widget* widget, Thread& thread);
	no_copy(DRMWindow);
	~DRMWindow();

	// Display
	void event() override;
	void setCursor(Cursor cursor) override;

	// Input
	void mouseEvent(int2, ::Event, Button);
	void keyPress(Key);

	// IPC
	String getSelection(bool clipboard) override;
};

unique<Window> window(Widget* widget, int2 size=-1, Thread& thread=mainThread);
