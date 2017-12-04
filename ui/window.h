#pragma once
/// \file window.h Window display and input
#include "display.h"
#include "widget.h"
#include "function.h"
#include "map.h"
#include "time.h"
#include "x.h"
//namespace X11 { struct Event; }

struct Window : Poll {
 /// Widget managed by this window
 Widget* widget;

 // Display
 /// Window size
 uint2 size = 0;
 /// Background color
 bgr3f backgroundColor = 0;
 /// Current cursor
 MouseCursor cursor = MouseCursor::Arrow;

 bool update = true;

 // Window
 /// Whether this window is currently mapped. This doesn't imply the window is visible (can be covered)
 bool mapped = false;
 /// Shows window.
 virtual void show() abstract;
 /// Hides window.
 virtual void hide() abstract;

 /// Sets window title to \a title
 virtual void setTitle(const string title) abstract;
 /// Sets window icon to \a icon
 virtual void setIcon(const Image& icon) abstract;
 virtual void setCursor(MouseCursor cursor) abstract;
 /// Resizes window to \a size
 virtual void setSize(int2 size=-1) abstract;

 // Input
 /// Actions triggered when a key is pressed
 map<Key, function<void()>> actions;
 /// Current widget that has the keyboard input focus
 Widget* focus = widget;
 /// Current widget that has the drag focus
 Widget* drag = 0;

 function<void()> presentComplete;

 Time swapTime {false};

 Window(Widget* widget, Thread& thread, int2 size = 0) : Poll(0,0,thread), widget(widget), size(size) {}
 virtual ~Window() {}

 // Display
 /// Schedules window rendering after all events have been processed (\sa Poll::queue)
 void render();
 /// Immediately renders the first pending update to target
 virtual void render(const Image& target);

 // Control
 virtual function<void()>& globalAction(Key) abstract;
 virtual String getSelection(bool clipboard) abstract;
 virtual void setSelection(string selection, bool clipboard) abstract;
};

/// Interfaces \a widget as a window on a display server
struct XWindow : Window, XDisplay /*should reference but inherits for convenience*/ {
 /// Window title
 String title;
 function<String()> getTitle;

 /// Associated window resource (relative to resource ID base Display::id)
 enum Resource { Window, GraphicContext, Colormap, PresentEvent, Segment, Pixmap, Picture, Cursor, CursorPixmap };
 /// System V shared memory
 int shm = 0;
 /// Rendering target in shared memory
 Image target;
 /// Shared window buffer state
 enum State { Idle, Copy, Present };
 State state = Idle;

 uint64 firstFrameCounterValue = 0;
 uint64 currentFrameCounterValue = 0;

 /// An event held to implement motion compression and ignore autorepeats
 unique<X11::Event> heldEvent = nullptr;

 /// bgra32 XRender PictFormat (for Cursor)
 uint format=0;

 String selection[2];

 // GLX/Xlib/DRI2
 struct __GLXFBConfigRec* fbConfig = 0;
 typedef struct __GLXcontextRec* GLXContext;
 GLXContext glContext = 0;

 /// Creates an initially hidden window for \a widget, use \a show to display
 /// \note size admits special values: 0 means fullscreen and negative \a size creates an expanding window)
 XWindow(Widget* widget=0, Thread& thread=mainThread, int2 size=0, int useGL_samples=0);
 /// Frees the graphics context and destroys the window
 ~XWindow();

 // Input
 /// Processes or holds an event
 void onEvent(const ref<byte> ge);
 /// Processes an event
 bool processEvent(const X11::Event& ge);

 // Window
 void show() override;
 void hide() override;

 /// Sets window title to \a title
 void setTitle(const string title) override;
 /// Sets window icon to \a icon
 void setIcon(const Image& icon) override;
 /// Resizes window to \a size
 void setSize(int2 size) override;

 // Display
 void event() override;

 /// Makes a new shared GL context current
 GLXContext initializeThreadGLContext();

 void setCursor(MouseCursor cursor) override;

 // Control
 /// Registers global action on \a key
 function<void()>& globalAction(Key) override;
 /// Gets current text selection
 /// \note The selection owner might lock this process if it fails to notify
 String getSelection(bool clipboard) override;
 void setSelection(string selection, bool clipboard) override;
};

unique<Window> window(Widget* widget, int2 size=-1, Thread& thread=mainThread, int useGL_samples=1, string title={});
