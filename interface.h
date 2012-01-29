#pragma once
#include "gl.h"
class Font;

/// Event type
enum Event { Motion, Press, Release, Enter, Leave };
/// Mouse button
enum Button { None, LeftButton, RightButton, MiddleButton, WheelDown, WheelUp };
/// Key code
#include "X11/keysym.h"
enum Key { Escape=XK_Escape, Return=XK_Return, Left=XK_Left, Right=XK_Right, Delete=XK_Delete, BackSpace=XK_BackSpace /*TODO: X keys*/ };

/// Widget is an abstract component to compose user interfaces
struct Widget {
/// Layout
    int2 position; /// position of the widget within its parent widget
    int2 size; /// size of the widget
    /// Preferred size (positive means preferred, negative means expanding (i.e benefit from extra space))
    /// \note space is first allocated to preferred widgets, then to expanding widgets.
    virtual int2 sizeHint() = 0;
    /// Notify objects to process \a position,\a size or derived member changes
    virtual void update() {}

/// Paint
    /// Renders this widget.
    /// use \a scale to transform window coordinates to OpenGL device coordinates (TODO: handle in GLShader?)
    /// \a offset is the absolute position of the parent widget
    virtual void render(int2 parent) =0;

/// Event
    /// Override \a mouseEvent to handle or forward user input
    /// \note \a mouseEvent is first called on the root \a Window::widget
    /// \return whether the mouse event should trigger a repaint
    virtual bool mouseEvent(int2 /*position*/, Event /*event*/, Button /*button*/) { return false; }
    /// Override \a keyPress to handle or forward user input
    /// \note \a keyPress is directly called on the current \a Window::focus
    /// \return whether the key press should trigger a repaint
    virtual bool keyPress(Key) { return false; }
};

/// Space is a proxy Widget to add space as needed
struct Space : Widget {
    /// desired empty space size (negative for expanding)
    int2 space;
    Space(int x=-1, int y=-1):space(x,y){}
    int2 sizeHint() { return space; }
    void render(int2) {}
};

/// Scroll is a proxy Widget containing a widget in a scrollable area.
//TODO: flick, scroll indicator, scrollbar
struct ScrollArea : Widget {
    /// Override \a widget to return the proxied widget
    virtual Widget& widget() =0;
    /// Ensures \a target is visible inside the region of the viewport
    /// \note Assumes \a target is a direct child of the proxied \a widget
    void ensureVisible(Widget& target);

    int2 sizeHint() { return widget().sizeHint(); }
    void update() override;
    bool mouseEvent(int2 position, Event event, Button button) override;
    void render(int2 parent) { return widget().render(parent+position); }
};

/// Scroll<T> implement a scrollable \a T by proxying it through \a ScrollArea
/// It allows an object to act both as the content (T) and the container (ScrollArea)
/// \note \a ScrollArea and \a T will be instancied as separate Widget (non-virtual multiple inheritance)
template<class T> struct Scroll : ScrollArea, T {
    Widget& widget() { return (T&)*this; }
    /// Return a reference to \a ScrollArea::Widget. e.g used when adding this widget to a layout
    Widget& parent() { return (ScrollArea&)*this; }
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
    /// Override \a count and \a at to implement widgets storage (\sa WidgetLayout ListLayout TupleLayout)
    virtual int count() =0;
    virtual Widget& at(int) =0;

    typedef index_iterator<Layout,Widget> iterator;
    iterator begin() { return iterator(this,0); }
    iterator end() { return iterator(this,count()); }

    /// Forwards event to intersecting child widgets until accepted
    bool mouseEvent(int2 position, Event event, Button button) override;
    /// Renders every child widget
    void render(int2 parent);
};

/// WidgetLayout implements Layout storage using array<Widget*> (i.e by reference)
/// \note It allows a layout to contain heterogenous Widget objects.
struct WidgetLayout : virtual Layout, array<Widget*> {
    int count() { return array<Widget*>::size; }
    Widget& at(int i) { return *array<Widget*>::at(i); }
    WidgetLayout& operator <<(Widget& w) { append(&w); return *this; }
};

/// ListLayout implements Layout storage using array<T> (i.e by value)
/// \note It allows a layout to directly contain homogenous items without managing an indirection.
template<class T> struct ListLayout : virtual Layout, array<T> {
    int count() { return array<T>::size; }
    Widget& at(int i) { return array<T>::at(i); }
    ListLayout& operator <<(T&& t) { array<T>::append(move(t)); return *this; }
};

/// Linear divide space between contained widgets
/// \note this is an abstract class, use \a Horizontal or \a Vertical
struct Linear : virtual Layout {
    /// If true, try to fill parent space to spread out contained items
    bool expanding = false;
    int2 sizeHint();
    void update() override;
    virtual int2 xy(int2 xy) =0; //transform coordinates so that x/y always mean along/across the line to reuse same code in Vertical/Horizontal
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

    bool mouseEvent(int2 position, Event event, Button button) override;
    void render(int2 parent) override;
};

/// ListSelection is an \a ListLayout with \a Selection
template<class T> struct ListSelection : ListLayout<T>, Selection {
    /// Return active item (last selection)
    inline T& active() { return array<T>::at(this->index); }
};

/// List is a \a Vertical layout of selectable items (\sa ListSelection)
template<class T> struct List : Scroll<Vertical>, ListSelection<T> {};
/// Bar is a \a Horizontal layout of selectable items (\sa ListSelection)
template<class T> struct Bar : Horizontal, ListSelection<T> {};

/// Text is a \a Widget displaying text (can be multiple lines)
struct Text : Widget {
    /// Create a caption that display \a text using a \a size pt (points) font
    Text(string&& text=""_, int size=16);

    /// Displayed text
    string text;

    int2 sizeHint();
    void update() override;
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

/// TextInput is an editable \a Text
//TODO: multiline
struct TextInput : Text {
protected:
    int cursor=0;

    void update() override;
    bool mouseEvent(int2 position, Event event, Button button) override;
    bool keyPress(Key key) override;
    void render(int2 parent);
};

/// Icon is a widget displaying a static image
struct Icon : Widget {
    Icon(){}
    /// Create a trigger button displaying \a image
    Icon(const Image& image);
    /// Displayed image
    GLTexture image;

    int2 sizeHint();
    void render(int2 parent);
};

/// TriggerButton is a clickable Icon
struct TriggerButton : Icon {
    //using Icon::Icon;
    TriggerButton(){}
    TriggerButton(const Image& image):Icon(image){}
    /// User clicked on the button
    signal<> triggered;
    bool mouseEvent(int2 position, Event event, Button button) override;
};

/// Item is an icon with text
//TODO: generic TupleLayout<> to store named tuples -> Item : Horizontal, TupleLayout<Icon icon,Text text>
struct Item : Horizontal {
    Icon icon; Text text; Space rightRag;
    Item() {}
    Item(Icon&& icon, Text&& text):icon(move(icon)),text(move(text)){}
    int count() { return 3; }
    Widget& at(int i) { return i==0?(Widget&)icon:i==1?(Widget&)text:rightRag; }
};
typedef Item Tab; //TODO: tab aspect

/// TabBar is a \a Bar of \a Tab items
typedef Bar<Tab> TabBar;

/// TriggerButton is a togglable Icon
//TODO: inherit Icon
struct ToggleButton : Widget {
    /// Create a toggle button showing \a enable icon when disabled or \a disable icon when enabled
	ToggleButton(const Image& enable,const Image& disable);

    /// User toggled the button
    signal<bool /*nextState*/> toggled;

    /// Current button state
    bool enabled = false;

    int2 sizeHint();
    void render(int2 parent);
    bool mouseEvent(int2 position, Event event, Button button) override;

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
    bool mouseEvent(int2 position, Event event, Button button) override;

protected:
    static const int height = 32;
};

/// Declare a small .png icon embedded in the binary, decoded on startup and accessible at runtime as an Image
/// \note an icon with the same name must be linked by the build system
///       'ld -r -b binary -o name.o name.png' can be used to embed a file in the binary
#define ICON(name) \
    extern byte _binary_icons_## name ##_png_start[]; \
    extern byte _binary_icons_## name ##_png_end[]; \
    static Image name ## Icon (array<byte>(_binary_icons_## name ##_png_start,_binary_icons_## name ##_png_end-_binary_icons_## name ##_png_start))
