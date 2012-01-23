#pragma once
#include "string.h"
#include "gl.h"
#include "process.h"
#include "font.h"

/// Event type, mouse button or key
enum Event { Motion, LeftButton, RightButton, MiddleButton, WheelDown, WheelUp, Quit /*TODO: X keys*/ };
/// State of \a event (or LeftButton for Motion events)
enum State { Released=0, Pressed=1 };

/// Widget is an abstract component to compose user interfaces
struct Widget {
/// Layout
    int2 position; /// position of the widget within its parent widget
    int2 size; /// size of the widget
    /// Preferred size (positive means fixed size, negative means minimum size (i.e benefit from extra space to expand))
    virtual int2 sizeHint() = 0;
    /// Notify objects to process \a position,\a size or derived member changes
    virtual void update() {}

/// Paint
    /// Render this widget.
    /// use \a scale to transform window coordinates to OpenGL device coordinates (TODO: handle in GLShader?)
    /// \a offset is the absolute position of the parent widget
    virtual void render(int2 parent) =0;

/// Event
    /// Notify objects to process a new user event
    virtual bool event(int2 /*position*/, Event /*event*/, State /*state*/) { return false; }
};

//->window.h
/// Window display \a widget in an X11 window, initialize its GL context and forward events.
//forward declarations from Xlib.h
typedef struct _XDisplay Display;
typedef struct __GLXcontextRec* GLXContext;
typedef unsigned long XWindow;
struct Window : Poll {
    /// Display \a widget in a window
    /// \a name is the application class name (WM_CLASS)
    /// \note a Window must be created before OpenGL can be used (e.g most Widget constructors need a GL context)
    /// \note Make sure the referenced widget is initialized before running this constructor
    Window(Widget& widget, int2 size, const string& name);
    /// Repaint window contents. Also called when an event was accepted by a widget.
    void render();

    /// Show/Hide window
    void setVisible(bool visible=true);
    /// Current visibility
    bool visible = false;

    /// Resize window to \a size
    void resize(int2 size);
    /// Toggle windowed/fullscreen mode
    void setFullscreen(bool fullscreen=true);
    /// Rename window to \a name
    void rename(const string& name);
    /// Set window icon to \a icon
    void setIcon(const Image& icon);

    template<class T> void setProperty(const char* type,const char* name, const array<T>& value);

    /// Register global shortcut named \a key (X11 KeySym)
    uint addHotKey(const string& key);
    /// User pressed a key (including global hot keys)
    signal<Event> keyPress;
    /// X11 window ID
    XWindow id;
protected:
    pollfd poll();
    void event(pollfd);

    Display* x;
    GLXContext ctx;
    Widget& widget;
};

/// Space is a proxy Widget to add space as needed
struct Space : Widget {
    /// desired empty space size (negative for expanding)
    int2 space;
    Space(int x, int y):space(x,y){}
    int2 sizeHint() { return space; }
    void render(int2) {}
};

/// Scroll is a proxy Widget containing a single widget in a scrollable area.
//TODO: scroll indicator (+flick) or scrollbar
struct ScrollArea : Widget {
    /// override \a widget to return the proxied widget
    virtual Widget& widget() =0;

    int2 sizeHint() { return widget().sizeHint(); }
    void update() override {
        if(size>=widget().sizeHint()) {
            widget().position=int2(0,0);
            widget().size = size;
        } else {
            widget().position = max(size-widget().sizeHint(),min(int2(0,0),widget().position));
            widget().size=max(widget().sizeHint(),size);
        }
        widget().update();
    }
    bool event(int2 position, Event event, State state) override;
    void render(int2 parent) { return widget().render(parent+position); }
};

/// Scroll<T> implement a scrollable \a T by proxying it through \a ScrollArea
/// It allow an object to act both as the content (T) and the container (ScrollArea)
/// \note \a ScrollArea and \a T will be instancied as separate Widget (non-virtual multiple inheritance)
template<class T> struct Scroll : ScrollArea, T {
    Widget& widget() { return (T&)*this; }
    Widget& parent() { return (ScrollArea&)*this; }
    /// Return a reference to \a ScrollArea::Widget. e.g used when adding this widget to a layout
    Widget* operator &() const { return (ScrollArea*)this; }
};

/// index_iterator is used to iterate indexable containers
template<class C, class T> struct index_iterator {
    C* container;
    int index;
    index_iterator(C* container, int index):container(container),index(index){}
    bool operator!=(const index_iterator& o) const { assert(container==o.container); return o.index != index; }
    T& operator* () const { assert(container); return container->at(index); }
    const index_iterator& operator++ () { index++; return *this; }
};

/// Layout is a proxy Widget containing multiple widgets.
struct Layout : Widget {
    /// override \a count and \a at to implement widgets storage (\sa WidgetLayout ItemLayout Tab)
    virtual int count() =0;
    virtual Widget& at(int) =0;

    typedef index_iterator<Layout,Widget> iterator;
    iterator begin() { return iterator(this,0); }
    iterator end() { return iterator(this,count()); }

    /// Forward event to intersecting child widgets until accepted
    bool event(int2 position, Event event, State state) override;
    /// Render every child widget
    void render(int2 parent);
};

/// WidgetLayout implements Layout storage using array<Widget*> (i.e by reference)
/// \note It allows a layout to contain heterogenous Widget objects.
struct WidgetLayout : virtual Layout, array<Widget*> {
    int count() { return array<Widget*>::size; }
    Widget& at(int i) { return *array<Widget*>::at(i); }
    WidgetLayout& operator <<(Widget& w) { append(&w); return *this; }
};

/// ItemLayout implements Layout storage using array<T> (i.e by value)
/// \note It allows a layout to directly contain homogenous items without managing an indirection.
template<class T> struct ItemLayout : virtual Layout, array<T> {
    int count() { return array<T>::size; }
    Widget& at(int i) { return array<T>::at(i); }
    ItemLayout& operator <<(T&& t) { array<T>::append(move(t)); return *this; }
};

/// Linear divide space between contained widgets
/// \note this is an abstract class, use \a Horizontal or \a Vertical
struct Linear : virtual Layout {
    /// Try to fill parent space to spread out contained items
    bool expanding = false;
    int2 sizeHint();
    void update() override;
    virtual int2 xy(int2 xy) =0; //overriden to reuse the same code for horizontal/vertical
};

/// Horizontal divide horizontal space between contained widgets
struct Horizontal : virtual Linear {
    int2 xy(int2 xy) override { return xy; }
};
/// HBox is a \a Horizontal layout of heterogenous widgets (\sa WidgetLayout)
struct HBox : Horizontal, WidgetLayout {};

/// Vertical divide vertical space between contained widgets
struct Vertical : virtual Linear {
    int2 xy(int2 xy) override { return int2(xy.y,xy.x); }
};
/// VBox is a \a Vertical layout of heterogenous widgets (\sa WidgetLayout)
struct VBox : Vertical, WidgetLayout {};

/// Selection implements selection of active widget/item for a \a Layout
//TODO: multi selection
struct Selection : virtual Layout {
    /// User changed active index.
    signal<int /*active index*/> activeChanged;

    /// Active index
    int index = -1;

    bool event(int2 position, Event event, State state) override;
    void render(int2 parent) override;
};

/// ItemSelection is an \a ItemLayout with \a Selection
template<class T> struct ItemSelection : ItemLayout<T>, Selection {
    /// Return active item (last selection)
    inline T& active() { return array<T>::at(this->index); }
};

/// List is a \a Vertical layout of selectable items (\sa ItemSelection)
template<class T> struct List : Scroll<Vertical>, ItemSelection<T> {};
//template<class T> struct List : Vertical, ItemSelection<T> {};
/// Bar is a \a Horizontal layout of selectable items (\sa ItemSelection)
template<class T> struct Bar : Horizontal, ItemSelection<T> {};

/// Text is a \a Widget displaying text (can be multiple lines)
struct Text : Widget {
    /// Create a caption that display \a text using a \a size pt (points) font
    Text(int size=16, string&& text=""_);
    /// Set the text to display
    void setText(string&& text);

    /// Displayed text
    string text;

    int2 sizeHint();
    void render(int2 parent);

protected:
    int size;
    Font& font;
    int2 textSize;
    struct Blit { vec2 min, max; uint id; };
    array<Blit> blits;
};

/// TextList is a \a List of \a Text items
typedef List<Text> TextList;

/// TriggerButton is a clickable image Widget
struct TriggerButton : Widget {
    TriggerButton() {}
    /// Create a trigger button showing \a icon
    TriggerButton(const Image& icon);

    /// User clicked on the button
    signal<> triggered;

    int2 sizeHint();
    void render(int2 parent);
    bool event(int2 position, Event event, State state) override;

protected:
    GLTexture icon;
};
typedef TriggerButton Icon;

/// Tab is an icon with text
struct Tab : Horizontal {
    Icon icon; Space margin{4,0}; Text text;
    int count() { return 3; }
    Widget& at(int i) { return i==0?(Widget&)icon:i==1?(Widget&)margin:(Widget&)text; }//TODO:named tuple
};

/// TabBar is a \a Bar of \a Tab items
typedef Bar<Tab> TabBar;

/// TriggerButton is a togglable image Widget
struct ToggleButton : Widget {
    /// Create a toggle button showing \a enable icon when disabled or \a disable icon when enabled
	ToggleButton(const Image& enable,const Image& disable);

    /// User toggled the button
    signal<bool /*nextState*/> toggled;

    /// Current button state
    bool enabled = false;

    int2 sizeHint();
    void render(int2 parent);
    bool event(int2 position, Event event, State state) override;

protected:
    GLTexture enableIcon, disableIcon;
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

    int2 sizeHint();
    void render(int2 parent);
    bool event(int2 position, Event event, State state) override;

protected:
    static const int height = 32;
};

/// Declare a small .png icon embedded in the binary, decoded on startup and accessible at runtime as an Image
/// \note an icon with the same name must be linked by the build system
///       'ld -r -b binary -o name.o name.png' can be used to embed a file in the binary
#define ICON(name) \
    extern byte _binary_## name ##_png_start[]; \
    extern byte _binary_## name ##_png_end[]; \
    static Image name ## Icon (array<byte>(_binary_## name ##_png_start,_binary_## name ##_png_end-_binary_## name ##_png_start))
