#pragma once
#include "widget.h"
#include "function.h"

/// Layout is a proxy Widget containing multiple widgets.
struct Layout : Widget {
    /// Override \a count and \a at to implement widgets storage (\sa Widgets Array Tuple)
    virtual uint count() const =0;
    virtual Widget& at(int) =0;

    /// Computes widgets layout
    virtual array<Rect> layout(int2 position, int2 size)=0;

    /// Renders all visible child widgets
    void render(int2 position, int2 size) override;
    /// Forwards event to intersecting child widgets until accepted
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
};

/// Widgets implements Layout storage using array<Widget*> (i.e by reference)
/// \note It allows a layout to contain heterogenous Widget objects.
struct Widgets : virtual Layout, array<Widget*> {
    Widgets(){}
    Widgets(const ref<Widget*>& widgets):array(widgets){}
    uint count() const { return array::size(); }
    Widget& at(int i)  { return *array::at(i); }
};

/// Array implements Layout storage using array<T> (i.e by value)
/// \note It allows a layout to directly contain homogenous items without managing pointers.
template<class T> struct Array : virtual Layout, array<T> {
    Array(){}
    Array(array<T>&& items) : array<T>(move(items)){}
    uint count() const { return array<T>::size(); }
    Widget& at(int i) { return array<T>::at(i); }
};

/// Linear divides space between contained widgets
/// \note This is an abstract class, use \a Horizontal or \a Vertical
struct Linear: virtual Layout {
    /// Expands main axis even when no widget is expanding
    bool expanding = false;
    /// How to use any extra space when no widget is expanding
    enum Extra {
        Left,Top=Left, /// Aligns tightly packed widgets
        Right,Bottom=Right, /// Aligns tightly packed widgets
        Center, /// Aligns tightly packed widgets
        Share,  /// Only for main axis, shares space evenly between all widgets (fixed size widgets will center within their extra space)
        Spread, /// Only for main axis, spreads widgets evenly leaving no outside margin (use \a Share to leave outside margin)
        AlignLeft,AlignTop=AlignLeft, /// Only for side axis, sets all widgets side size to maximum needed and aligns left/top
        AlignRight,AlignBottom=AlignRight, /// Only for side axis, sets all widgets side size to maximum needed and aligns right/bottom
        AlignCenter, /// Only for side axis, sets all widgets side size to largest hint (or total available if any widget is expanding) and centers
        Expand /// Only for side axis, sets all widgets side size to layout available side size
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

/// Horizontal divides horizontal space between contained widgets
struct Horizontal : virtual Linear {
    int2 xy(int2 xy) override { return xy; }
};
/// Vertical divides vertical space between contained widgets
struct Vertical : virtual Linear{
    int2 xy(int2 xy) override { return int2(xy.y,xy.x); }
};

/// HBox is a \a Horizontal layout of heterogenous widgets (\sa Widgets)
struct HBox : virtual Horizontal, virtual Widgets {
    HBox(){}
    HBox(const ref<Widget*>& widgets):Widgets(widgets){}
};
/// VBox is a \a Vertical layout of heterogenous widgets (\sa Widgets)
struct VBox : Vertical, Widgets {
    VBox(){}
    VBox(const ref<Widget*>& widgets):Widgets(widgets){}
};
/// HList is a \a Horizontal layout of items (\sa Array)
template<class T> struct HList : Horizontal, Array<T> {
    HList(){}
    HList(array<T>&& widgets):Array<T>(move(widgets)){}
};
/// VList is a \a Vertical layout of items (\sa Array)
template<class T> struct VList : Vertical, Array<T> {
    VList(){}
    VList(array<T>&& widgets):Array<T>(move(widgets)){}
};

/// UniformGrid layouts items on an uniform \a width x \a height grid
struct Grid : virtual Layout {
    /// Horizontal and vertical element count, 0 means automatic
    int width,height;
    Grid(int width=0, int height=0):width(width),height(height){}
    int2 sizeHint();
    array<Rect> layout(int2 position, int2 size) override;
};
template<class T> struct UniformGrid : Grid,  Array<T> {

};

/// Selection implements selection of active widget/item for a \a Layout
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
    bool keyPress(Key key) override;
};

/// Displays a selection using a blue highlight
struct HighlightSelection : virtual Selection {
    void render(int2 position, int2 size) override;
};

/// Displays a selection using horizontal tabs
struct TabSelection : virtual Selection {
    void render(int2 position, int2 size) override;
};

/// ArraySelection is an \a Array with \a Selection
template<class T> struct ArraySelection : Array<T>, virtual Selection {
    ArraySelection(){}
    ArraySelection(array<T>&& items) : Array<T>(move(items)){}
    /// Return active item (last selection)
    T& active() { return array<T>::at(this->index); }
};

/// List is a \a Vertical layout of selectable items (\sa ArraySelection)
template<class T> struct List : Vertical, ArraySelection<T>, HighlightSelection {
    List(){}
    List(array<T>&& items) : ArraySelection<T>(move(items)){}
};
/// Bar is a \a Horizontal layout of selectable items (\sa ArraySelection)
template<class T> struct Bar : Horizontal, ArraySelection<T>, TabSelection {
    Bar(){}
    Bar(array<T>&& items) : ArraySelection<T>(move(items)){}
};
/// GridSelection is a \a Grid layout of selectable items (\sa ArraySelection)
template<class T> struct GridSelection : Grid, ArraySelection<T>, HighlightSelection {
    GridSelection(int width=0, int height=0) : Grid(width,height){}
    GridSelection(array<T>&& items) : ArraySelection<T>(move(items)){}
};
