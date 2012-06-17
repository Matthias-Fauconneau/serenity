#pragma once
#include "widget.h"
#include "signal.h"

/// Layout is a proxy Widget containing multiple widgets.
struct Layout : Widget {
    /// Override \a count and \a at to implement widgets storage (\sa Widgets Array Tuple)
    virtual uint count() const =0;
    virtual Widget& at(int) =0;

    /// Forwards event to intersecting child widgets until accepted
    bool mouseEvent(int2 position, Event event, Button button) override;
    /// Renders every child widget
    void render(int2 parent);
};

/// Widgets implements Layout storage using array<Widget*> (i.e by reference)
/// \note It allows a layout to contain heterogenous Widget objects.
struct Widgets : virtual Layout, array<Widget*> {
    Widgets(){}
    Widgets(initializer_list<Widget*>&& widgets):array(move(widgets)){}
    uint count() const;
    Widget& at(int i);
};

/// Array implements Layout storage using array<T> (i.e by value)
/// \note It allows a layout to directly contain homogenous items without managing pointers.
template<class T> struct Array : virtual Layout, array<T> {
    Array(){}
    Array(array<T>&& items) : array<T>(move(items)){}
    uint count() const { return array<T>::size(); }
    Widget& at(int i) { return array<T>::at(i); }
};

template<class T> struct item : T { //item instanciate a class and append the instance to the offset table
    void registerInstance(int* list) { int* first=list; while(*first) first++; *first=(byte*)this-(byte*)list; }
    item(int* list) { registerInstance(list); }
    item(T&& t, int* list) : T(move(t)) { registerInstance(list); }
};
/// \a tuple with static indexing by type and dynamic indexing using an offset table
template<class... T> struct tuple  : item<T>... {
    int offsets[sizeof...(T)] = {};
    tuple() : item<T>(offsets)... {}
    tuple(T&&... t) : item<T>(move(t),offsets)... {}
    int size() const { return sizeof...(T); }
    template<class A> A& get() { return static_cast<A&>(*this); }
    template<class A> const A& get() const { return static_cast<const A&>(*this); }
    void* at(int i) { return (void*)((byte*)offsets+offsets[i]); }
};

/// Tuple implements Layout storage using static inheritance
/// \note It allows a layout to directly contain heterogenous Widgets without managing heap pointers.
template<class... T> struct Tuple : virtual Layout {
    tuple<T...> items; //inheriting tuple confuse compiler with multiple Widgets base
    Tuple() : items() {}
    Tuple(T&& ___ t) : items(move(t)___) {}
    Widget& at(int i) { return *(Widget*)items.at(i); }
    uint count() const { return items.size(); }
    template<class A> A& get() { return items.template get<A>(); }
    template<class A> const A& get() const { return items.template get<A>(); }
};

/// Linear divide space between contained widgets
/// \note this is an abstract class, use \a Horizontal or \a Vertical
struct Linear: virtual Layout {
    /// If true, try to fill parent space to spread out contained items
    bool expanding = false;
    /// Align to { -1 = left/top, 0 = center, 1 = right/bottom }
    int align=0;

    int2 sizeHint() override;
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
    HBox(){}
    HBox(initializer_list<Widget*>&& widgets):Widgets(move(widgets)){}
};
/// VBox is a \a Vertical layout of heterogenous widgets (\sa Widgets)
struct VBox : Vertical, Widgets {
    VBox(){}
    VBox(initializer_list<Widget*>&& widgets):Widgets(move(widgets)){}
};

template<class T> struct HList : Horizontal, Array<T> {
    HList(){}
    HList(initializer_list<T>&& widgets):Array<T>(move(widgets)){}
};
template<class T> struct VList : Vertical, Array<T> {
    VList(){}
    VList(initializer_list<T>&& widgets):Array<T>(move(widgets)){}
};

/// Layout items on an uniform \a width x \a height grid
struct UniformGrid : virtual Layout {
    /// horizontal and vertical element count, 0 means automatic
    int width,height;
    UniformGrid(int width=0, int height=0):width(width),height(height){}
    int2 sizeHint();
    void update();
};

/// Selection implements selection of active widget/item for a \a Layout
struct Selection : virtual Layout {
    /// User changed active index.
    signal<int /*index*/> activeChanged;
    /// Active index
    uint index = -1;
    /// Set active index and emit activeChanged
    void setActive(uint index);
    /// User clicked on an item.
    signal<int /*index*/> itemPressed;

    bool mouseEvent(int2 position, Event event, Button button) override;
    bool keyPress(Key key) override;
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
    ListSelection(array<T>&& items) : Array<T>(move(items)){}
    /// Return active item (last selection)
    inline T& active() { return array<T>::at(this->index); }
};

/// List is a \a Vertical layout of selectable items (\sa ListSelection)
template<class T> struct List : Vertical, ListSelection<T>, HighlightSelection {
    List(){}
    List(initializer_list<T>&& items) : ListSelection<T>(move(items)){}
};
/// Bar is a \a Horizontal layout of selectable items (\sa ListSelection)
template<class T> struct Bar : Horizontal, ListSelection<T>, TabSelection {
    Bar(){}
    Bar(initializer_list<T>&& items) : ListSelection<T>(move(items)){}
};
/// Grid is an \a UniformGrid layout of selectable items (\sa ListSelection)
template<class T> struct Grid : UniformGrid, ListSelection<T>, HighlightSelection {
    Grid(int width, int height):UniformGrid(width,height){}
    Grid(array<T>&& items) : ListSelection<T>(move(items)){}
};
