#pragma once
#include "string.h"
#include "gl.h"
#include "process.h"
#include "font.h"

/// Widget is an abstract component to compose user interfaces

struct Widget {
/// Layout
	int2 position, size;
	/// Preferred size (0 means expand)
	virtual int2 sizeHint() { return int2(0,0); }
    /// Notify objects to process \a position,\a size or derived member changes
    virtual void update() {}

/// Paint
    /// Render this widget. use \a scale and \a offset in implementation to scale from widget coordinates (0-size) to viewport
    virtual void render(vec2 scale, vec2 offset) =0;

/// Event
    /// Event type, mouse button or key
    enum Event { Motion, LeftButton, RightButton, MiddleButton, WheelDown, WheelUp /*TODO: X keys*/ };
    /// State of \a event (or LeftButton for Motion events)
    enum State { Released=0, Pressed=1 };
    /// Notify objects to process a new user event
    virtual bool event(int2 /*position*/, Event /*event*/, State /*state*/) { return false; }
};

/// Window display \a widget in an X11 window, initialize its GL context and forward events.
//forward declarations from Xlib.h
typedef struct _XDisplay Display;
typedef struct __GLXcontextRec* GLXContext;
typedef unsigned long XWindow;
struct Window : Poll {
    /// Create a \a size big window to host \a widget
    Window(int2 size, Widget& widget);
    /// Repaint window contents. Also called when an event was accepted by a widget.
    void render();

    /// Show/Hide window
    void setVisible(bool visible=true);
    /// Resize window to \a size
    void resize(int2 size);
    /// Toggle windowed/fullscreen mode
    void setFullscreen(bool fullscreen=true);
    /// Rename window to \a name
    void rename(const string& name);

    /// Register global shortcut named \a key (X11 KeySym)
    uint addHotKey(const string& key);
    /// User triggerred a registered hot key
    signal<uint /*KeySym*/> hotKeyTriggered;
protected:
    pollfd poll() override;
    bool event(pollfd) override;

    Display* x;
    GLXContext ctx;
    XWindow window;
    bool visible=true;
    Widget& widget;
};

/// Layout is a pure Widget containing children.
struct Layout : Widget {
    /// \a begin, \a end, \a count and \a at allow to specialize child widgets storage (\sa WidgetLayout ItemLayout)
	virtual virtual_iterator<Widget> begin() =0;
	virtual virtual_iterator<Widget> end() =0;
	virtual int count() =0;
	virtual Widget& at(int) =0;

    /// Forward event to intersecting child widgets until accepted
    bool event(int2 position, Event event, State state) {
		for(auto& child : *this) {
			if(position > child.position && position < child.position+child.size) {
				if(child.event(position-child.position,event,state)) return true;
			}
		}
		return false;
	}
    /// Render every child widget
	void render(vec2 scale, vec2 offset) {
		for(auto& child : *this) child.render(scale,offset+vec2(child.position)*scale);
	}
};

/// WidgetLayout implements Layout storage using array<Widget*> (i.e by reference)
/// \note It allows a layout to contain heterogenous Widget objects.
template<class L> struct WidgetLayout : L {
	array<Widget*> widgets;
	virtual_iterator<Widget> begin() { return new dereference_iterator<Widget>(widgets.begin()); }
	virtual_iterator<Widget> end() { return new dereference_iterator<Widget>(widgets.end()); }
	int count() { return widgets.size; }
	WidgetLayout& operator <<(Widget* w) { widgets << w; return *this; }
	Widget& at(int i) { return *widgets[i]; }
};

/// ItemLayout implements Layout storage using array<T> (i.e by value)
/// \note It allows a layout to directly contain homogenous items without managing an indirection.
template<class L, class T> struct ItemLayout : L {
	array<T> items;
	virtual_iterator<Widget> begin() { return new value_iterator<Widget>(items.begin()); }
	virtual_iterator<Widget> end() { return new value_iterator<Widget>(items.end()); }
	int count() { return items.size; }
	ItemLayout& operator <<(T&& t) { items << move(t); return *this; }
	Widget& at(int i) { return items[i]; }
};

/// Horizontal divide horizontal space between contained widgets
//TODO: factorize scrolling from Vertical
struct Horizontal : Layout {
protected:
    int2 sizeHint() override;
    void update() override;

    static const int margin = 1;
};
/// HBox layouts heterogenous widgets (i.e references) horizontally
typedef WidgetLayout<Horizontal> HBox;

/// Vertical divide vertical space between contained widgets, with scrolling if necessary
struct Vertical : Layout {
protected:
    int2 sizeHint() override;
    void update() override;
    bool event(int2 position, Event event, State state) override;
    void render(vec2 scale, vec2 offset) override;

    int margin = 0;
    int scroll = 0;
    bool mayScroll = false;
    int first = 0, last = 0;
};
/// VBox layouts heterogenous widgets (i.e references) vertically
typedef WidgetLayout<Vertical> VBox;

/// List is a \a Vertical layout with selection
//TODO: multi selection
struct List : Vertical {
    /// User changed active index.
    signal<int /*active index*/> activeChanged;

protected:
    bool event(int2 position, Event event, State state) override;
    void render(vec2 scale, vec2 offset) override;

    int index = -1;
};

/// ValueList is a \a List of homogenous items (i.e values)
template<class T> struct ValueList : ItemLayout<List, T> {
    /// Return active item (last selection)
    inline T& active() { return this->items[this->index]; }
};

/// Text is a \a Widget displaying text (can be multiple lines)
struct Text : Widget {
    /// Create a caption that display \a text using a \a size pt (points) font
    Text(int size, string&& text=string());
    /// Set the text to display
    void setText(string&& text);

protected:
    int2 sizeHint() override;
    void render(vec2 scale, vec2 offset) override;

    int size;
    Font& font;
    string text;
    int2 textSize;
    struct Blit { vec2 min, max; uint id; };
    array<Blit> blits;
};
/// TextList is a \a ValueList of \a Text items
typedef ValueList<Text> TextList;

/// TriggerButton is a clickable image Widget
struct TriggerButton : Widget {
    /// Create a trigger button showing \a icon
    TriggerButton(const Image& icon);

    /// User clicked on the button
    signal<> triggered;

protected:
    int2 sizeHint() override;
    void render(vec2 scale, vec2 offset) override;
    bool event(int2, Event event, State state) override;

    GLTexture icon;
    const int size = 32;
};

/// TriggerButton is a togglable image Widget
struct ToggleButton : Widget {
    /// Create a toggle button showing \a enable icon when disabled or \a disable icon when enabled
	ToggleButton(const Image& enable,const Image& disable);

    /// User toggled the button
    signal<bool /*nextState*/> toggled;

protected:
    int2 sizeHint() override;
    void render(vec2 scale, vec2 offset) override;
    bool event(int2, Event event, State state) override;

    GLTexture enableIcon, disableIcon;
    bool enabled = false;
    static const int size = 32;
};

/// Slider is a Widget to show or control a bounded value
struct Slider : Widget {
    /// \a minimum and \a maximum displayable/settable value
    int minimum = 0, maximum = 0;
    /// current \a value shown by the slider
    int value = -1;
    /// User edited the \a value
	signal<int> valueChanged;

protected:
    int2 sizeHint() override;
    void render(vec2 scale, vec2 offset) override;
    bool event(int2 position, Event event, State state) override;

    static const int height = 32;
};

/// Declare a small .png icon embedded in the binary, decoded on startup and accessible at runtime as an Image
/// \note an icon with the same name must be linked by the build system
///       'ld -r -b binary -o name.o name.png' can be used to embed a file in the binary
#define ICON(name) \
	extern uint8 _binary_## name ##_png_start[]; \
	extern uint8 _binary_## name ##_png_end[]; \
    static Image name ## Icon (_binary_## name ##_png_start,_binary_## name ##_png_end-_binary_## name ##_png_start)
