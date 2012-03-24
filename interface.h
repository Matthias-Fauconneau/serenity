#pragma once
#include "signal.h"
#include "vector.h"
#include "image.h"

/// Event type
enum Event { Motion, Press, Release, Enter, Leave };
/// Mouse button
enum Button { None, LeftButton, MiddleButton, RightButton, WheelDown, WheelUp };
/// Key code
#include "X11/keysym.h"
enum Key { Escape=XK_Escape, Return=XK_Return, Left=XK_Left, Right=XK_Right, Delete=XK_Delete, BackSpace=XK_BackSpace };

/// Widget is an abstract component to compose user interfaces
struct Widget {
/// Layout
    int2 position; /// position of the widget within its parent widget
    int2 size; /// size of the widget
    /// Preferred size (positive means preferred, negative means expanding (i.e benefit from extra space))
    /// \note space is first allocated to preferred widgets, then to expanding widgets.
    virtual int2 sizeHint() { return int2(0,0); }
    /// Notify objects to process \a position,\a size or derived member changes
    virtual void update() {}

/// Paint
    /// Renders this widget.
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
    int2 sizeHint() { return int2(-1,-1); }
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

/// Layout is a proxy Widget containing multiple widgets.
struct Layout : Widget {
    /// Override \a count and \a at to implement widgets storage (\sa Widgets Array Tuple)
    virtual int count() const =0;
    virtual Widget& at(int) =0;

    /// Forwards event to intersecting child widgets until accepted
    bool mouseEvent(int2 position, Event event, Button button) override;
    /// Renders every child widget
    void render(int2 parent);
};

/// Widgets implements Layout storage using array<Widget*> (i.e by reference)
/// \note It allows a layout to contain heterogenous Widget objects.
struct Widgets : virtual Layout, array<Widget*> {
    Widgets(std::initializer_list<Widget*>&& widgets):array(move(widgets)){}
    int count() const;
    Widget& at(int i);
};

/// Array implements Layout storage using array<T> (i.e by value)
/// \note It allows a layout to directly contain homogenous items without managing pointers.
template<class T> struct Array : virtual Layout, array<T> {
    Array(){}
    Array(std::initializer_list<T>&& items) : array<T>(move(items)){}
    int count() const { return array<T>::size(); }
    Widget& at(int i) { return array<T>::at(i); }
};

//convoluted template metaprogramming to get a static tuple allowing dynamic indexing using an offset table
struct zero { zero(int* list, uint size){clear(list,size);}}; //item "0" make sure the table is cleared so that items can append to list by finding first free slot
template<class T> struct item : T { //item instanciate a class and append the instance to the offset table
    void registerInstance(int* list) { int* first=list; while(*first) first++; *first=(byte*)this-(byte*)list; }
    item(int* list) { registerInstance(list); }
    item(T&& t, int* list) : T(move(t)) { registerInstance(list); }
};
/// \a tuple with static indexing by type and dynamic indexing using an offset table
template<class... T> struct tuple  : zero, item<T>... {
    int offsets[sizeof...(T)];
    tuple() : zero(offsets,sizeof...(T)), item<T>(offsets)... {}
    tuple(T&&... t) : zero(offsets,sizeof...(T)), item<T>(move(t),offsets)... {}
    int size() const { return sizeof...(T); }

    template<class A> A& get() { return static_cast<A&>(*this); }
    template<class A> const A& get() const { return static_cast<const A&>(*this); } //static indexing using type

    //template<class B> B& at(int i) { return *(B*)((byte*)offsets+offsets[i]); } //expected primary name before > ?
    void* at(int i) { return (void*)((byte*)offsets+offsets[i]); }
};

/// Tuple implements Layout storage using static inheritance
/// \note It allows a layout to directly contain heterogenous Widgets without managing heap pointers.
template<class... T> struct Tuple : virtual Layout {
    tuple<T...> items; //inheriting tuple confuse compiler with multiple Widgets base
    Tuple() : items() {}
    Tuple(T&&... t) : items(move(t)...) {}
    int count() const { return items.size(); }

    template<class A> A& get() { return items.get<A>(); } //static indexing using type
    template<class A> const A& get() const { return items.get<A>(); } //static indexing using type
    //template<class A> operator const A&() const { return items.operator A&(); } //static indexing using type

    Widget& at(int i) { return *(Widget*)items.at(i); }
    //Widget& at(int i) { return items.at<Widget>(i); } //expected primary name before > ?
};

/// Linear divide space between contained widgets
/// \note this is an abstract class, use \a Horizontal or \a Vertical
struct Linear: virtual Layout {
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
/// Vertical divide vertical space between contained widgets
struct Vertical : virtual Linear{
    int2 xy(int2 xy) override { return int2(xy.y,xy.x); }
};

/// HBox is a \a Horizontal layout of heterogenous widgets (\sa Widgets)
struct HBox : Horizontal, Widgets {
    HBox(std::initializer_list<Widget*>&& widgets):Widgets(move(widgets)){}
};
/// VBox is a \a Vertical layout of heterogenous widgets (\sa Widgets)
struct VBox : Vertical, Widgets {
    VBox(std::initializer_list<Widget*>&& widgets):Widgets(move(widgets)){}
};

/// Layout items on an uniform \a width x \a height grid
struct UniformGrid : virtual Layout {
    int width,height;
    UniformGrid(int width, int height):width(width),height(height){}
    int2 sizeHint();
    void update();
};

/// Selection implements selection of active widget/item for a \a Layout
struct Selection : virtual Layout {
    /// User clicked on an item.
    signal<int /*index*/> itemPressed;
    /// User changed active index.
    signal<int /*index*/> activeChanged;
    /// Active index
    int index = -1;

    bool mouseEvent(int2 position, Event event, Button button) override;
};

/// Displays a selection using a blue highlight
struct HighlightSelection : virtual Selection {
    void render(int2 parent) override;
};

/// Displays a selection using horizontal tabs
struct TabSelection : virtual Selection {
    void render(int2 parent) override;
};

/// ListSelection is an \a Array with \a Selection
template<class T> struct ListSelection : Array<T>, virtual Selection {
    ListSelection(){}
    ListSelection(std::initializer_list<T>&& items) : Array<T>(move(items)){}
    /// Return active item (last selection)
    inline T& active() { return array<T>::at(this->index); }
};

/// List is a \a Vertical layout of selectable items (\sa ListSelection)
template<class T> struct List : Vertical, ListSelection<T>, HighlightSelection {
    List(){}
    List(std::initializer_list<T>&& items) : ListSelection<T>(move(items)){}
};
/// Bar is a \a Horizontal layout of selectable items (\sa ListSelection)
template<class T> struct Bar : Horizontal, ListSelection<T>, TabSelection {
    Bar(){}
    Bar(std::initializer_list<T>&& items) : ListSelection<T>(move(items)){}
};
/// Grid is an \a UniformGrid layout of selectable items (\sa ListSelection)
template<class T> struct Grid : UniformGrid, ListSelection<T>, HighlightSelection {
    Grid(int width, int height):UniformGrid(width,height){}
};

/// Text is a \a Widget displaying text (can be multiple lines)
struct Text : Widget {
    /// Create a caption that display \a text using a \a size pt (points) font
    Text(string&& text=""_, int size=16, ubyte opacity=255);

    void setText(string&& text) { this->text=move(text); textSize=zero; }
    void setSize(int size) { this->size=size; textSize=zero; }

    /// Displayed text
    string text;
    /// Font size
    int size;
    /// Opacity
    ubyte opacity;
    /// Wrap
    bool wrap=false;

    int2 sizeHint();
    void update() override { update(wrap); }
    void update(bool wrap);
    void render(int2 parent);
protected:
    int2 textSize=zero;
    struct Blit { int2 pos; const Image& image; };
    array<Blit> blits;
};

/// TextList is a \a List of \a Text items
typedef List<Text> TextList;

/// TextInput is an editable \a Text
//TODO: multiline
struct TextInput : Text {
protected:
    uint cursor=0;

    bool mouseEvent(int2 position, Event event, Button button) override;
    bool keyPress(Key key) override;
    void render(int2 parent);
};

/// Icon is a widget displaying a static image
struct Icon : Widget {
    Icon(){}
    /// Create a trigger button displaying \a image
    Icon(Image&& image):image(move(image)){}
    /// Displayed image
    Image image;

    int2 sizeHint();
    void render(int2 parent);
};

/// Item is an icon with text
struct Item : Horizontal, Tuple<Icon,Text,Space> {
    Item():Tuple(){}
    Item(Icon&& icon, Text&& text):Tuple(move(icon),move(text),Space()){}
};

/// TabBar is a \a Bar containing \a Item elements
typedef Bar<Item> TabBar;

/// TriggerButton is a clickable Icon
struct TriggerButton : Icon {
    //using Icon::Icon;
    TriggerButton(){}
    TriggerButton(Image&& image):Icon(move(image)){}
    /// User clicked on the button
    signal<> triggered;
    bool mouseEvent(int2 position, Event event, Button button) override;
};

/// TriggerButton is a togglable Icon
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
    const Image& enableIcon;
    const Image& disableIcon;
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
