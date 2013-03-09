#pragma once
/// \file layout.h Widget layouts (Linear, Grid)
#include "widget.h"
#include "function.h"

/// Proxy Widget containing multiple widgets.
struct Layout : Widget {
    /// Derived classes should override \a count and \a at to implement widgets storage. \sa Widgets Array Tuple
    virtual uint count() const =0;
    virtual Widget& at(int) =0;

    /// Computes widgets layout
    virtual array<Rect> layout(int2 position, int2 size)=0;

    /// Renders all visible child widgets
    void render(int2 position, int2 size) override;
    /// Forwards event to intersecting child widgets until accepted
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
};

/// Implements Layout storage using array<Widget*> (i.e by reference)
/// \note It allows a layout to contain heterogenous Widget objects.
struct Widgets : virtual Layout, array<Widget*> {
    Widgets(){}
    Widgets(const ref<Widget*>& widgets):array(widgets){}
    uint count() const { return array::size; }
    Widget& at(int i)  { return *array::at(i); }
};

/// Implements Layout storage using array<T> (i.e by value)
/// \note It allows a layout to directly contain homogenous items without managing pointers.
template<class T> struct Array : virtual Layout, array<T> {
    Array(){}
    Array(array<T>&& items) : array<T>(move(items)){}
    uint count() const { return array<T>::size; }
    Widget& at(int i) { return array<T>::at(i); }
};

/// Layouts widgets on an axis
/// \note This is an abstract class, use \a Horizontal or \a Vertical
struct Linear: virtual Layout {
    /// Expands main axis even when no widget is expanding
    bool expanding = false;
    /// How to use any extra space when no widget is expanding
    enum Extra {
        Left,Top=Left, /// Aligns tightly packed widgets
        Right,Bottom=Right, /// Aligns tightly packed widgets
        Center, /// Aligns tightly packed widgets
        Even, /// shares space evenly
        Spread, /// allocates minimum sizes and spreads any extra space between widgets
        Share,  /// allocates minimum sizes and shares any extra space
        AlignLeft,AlignTop=AlignLeft, /// For side axis, sets all widgets side size to maximum needed and aligns left/top
        AlignRight,AlignBottom=AlignRight, /// For side axis, sets all widgets side size to maximum needed and aligns right/bottom
        AlignCenter, /// For side axis, sets all widgets side size to largest hint (or total available if any widget is expanding) and centers
        Expand /// For side axis, sets all widgets side size to layout available side size
    };
    Extra main, side;
    /// Constructs a linear layout
    /// \note This constructor should be used in most derived class (any initialization in derived classes are ignored)
    Linear(Extra main=Share, Extra side=AlignCenter):main(main),side(side){}

    int2 sizeHint() override;
    array<Rect> layout(int2 position, int2 size) override;
    /// Transforms coordinates so that x/y always means main/side (i.e along/across) axis to reuse same code in Vertical/Horizontal
    virtual int2 xy(int2 xy) =0;
};

/// Layouts widgets on the horizontal axis
struct Horizontal : virtual Linear {
    int2 xy(int2 xy) override { return xy; }
};
/// Layouts widgets on the vertical axis
struct Vertical : virtual Linear {
    int2 xy(int2 xy) override { return int2(xy.y,xy.x); }
};

/// Horizontal layout of heterogenous widgets. \sa Widgets
struct HBox : virtual Horizontal, virtual Widgets {
    HBox(){}
    HBox(const ref<Widget*>& widgets):Widgets(widgets){}
};
/// Vertical layout of heterogenous widgets. \sa Widgets
struct VBox : Vertical, Widgets {
    VBox(){}
    VBox(const ref<Widget*>& widgets):Widgets(widgets){}
};
/// Horizontal layout of homogenous items. \sa Array
template<class T> struct HList : Horizontal, Array<T> {
    HList(){}
    HList(array<T>&& widgets):Array<T>(move(widgets)){}
};
/// Vertical layout of homogenous items. \sa Array
template<class T> struct VList : Vertical, Array<T> {
    VList(){}
    VList(array<T>&& widgets):Array<T>(move(widgets)){}
};

/// Layouts items on an uniform #width x #height grid
struct Grid : virtual Layout {
    /// Horizontal element count, 0 means automatic
    int width;
    /// Vertical element count, 0 means automatic
    int height;
    /// Margin between elements
    int2 margin;
    Grid(int width=0, int height=0, int margin=0):width(width),height(height),margin(margin){}
    int2 sizeHint();
    array<Rect> layout(int2 position, int2 size) override;
};
template<class T> struct UniformGrid : Grid,  Array<T> {
    UniformGrid(int width=0, int height=0, int margin=0):Grid(width,height,margin){}
};

/// Implements selection of active widget/item for a \a Layout
struct Selection : virtual Layout {
    /// User changed active index.
    signal<uint /*index*/> activeChanged;
    /// Active index
    uint index = -1;
    /// Set active index and emit activeChanged
    void setActive(uint index);
    /// User clicked on an item.
    signal<uint /*index*/> itemPressed;

    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
    bool keyPress(Key key, Modifiers modifiers) override;
};

/// Displays a selection using a blue highlight
struct HighlightSelection : virtual Selection {
    /// Whether to always display the highlight or only when focused
    bool always=false;
    void render(int2 position, int2 size) override;
};

/// Displays a selection using horizontal tabs
struct TabSelection : virtual Selection {
    void render(int2 position, int2 size) override;
};

/// Array with Selection
template<class T> struct ArraySelection : Array<T>, virtual Selection {
    ArraySelection(){}
    ArraySelection(array<T>&& items) : Array<T>(move(items)){}
    /// Return active item (last selection)
    T& active() { return array<T>::at(this->index); }
};

/// Vertical layout of selectable items. \sa ArraySelection
template<class T> struct List : Vertical, ArraySelection<T>, HighlightSelection {
    List(){}
    List(array<T>&& items) : ArraySelection<T>(move(items)){}
};
/// Horizontal layout of selectable items. \sa ArraySelection
template<class T> struct Bar : Horizontal, ArraySelection<T>, TabSelection {
    Bar(){}
    Bar(array<T>&& items) : ArraySelection<T>(move(items)){}
};
/// GridSelection is a Grid layout of selectable items. \sa ArraySelection
template<class T> struct GridSelection : Grid, ArraySelection<T>, HighlightSelection {
    GridSelection(int width=0, int height=0, int margin=0) : Grid(width,height,margin){}
    GridSelection(array<T>&& items) : ArraySelection<T>(move(items)){}
};
