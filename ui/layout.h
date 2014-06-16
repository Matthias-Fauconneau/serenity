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
    virtual array<Rect> layout(int2 size)=0;

    /// Renders all visible child widgets
    void render() override;
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
    Array(const mref<T>& items) : array<T>(items){}
    Array(array<T>&& items) : array<T>(move(items)){}
    uint count() const { return array<T>::size; }
    Widget& at(int i) { return array<T>::at(i); }
};

/// Layouts widgets on an axis
/// \note This is an abstract class, use \a Horizontal or \a Vertical
struct Linear : virtual Layout {
    /// Expands main axis even when no widget is expanding
    bool expanding = false;
    /// How to use any extra space when no widget is expanding
    enum Extra {
        Left,Top=Left, /// Aligns tightly packed widgets
        Right,Bottom=Right, /// Aligns tightly packed widgets
        Center, /// Aligns tightly packed widgets
        Even, /// Shares space evenly
        Spread, /// Allocates minimum sizes and spreads any extra space between widgets
        Share, /// Allocates minimum sizes and shares any extra space between expanding widgets
        AlignLeft,AlignTop=AlignLeft, /// For side axis, sets all widgets side size to maximum needed and aligns left/top
        AlignRight,AlignBottom=AlignRight, /// For side axis, sets all widgets side size to maximum needed and aligns right/bottom
        AlignCenter, /// For side axis, sets all widgets side size to largest hint (or total available if any widget is expanding) and centers
        Expand /// For side axis, sets all widgets side size to layout available side size
    };
    Extra main, side;
    /// Identifier to help understand layout behaviour.
    string name;

    /// Constructs a linear layout
    /// \note This constructor should be used in most derived class (any initialization in derived classes are ignored)
    Linear(Extra main=Share, Extra side=AlignCenter):main(main),side(side){}

    int2 sizeHint() override;
    array<Rect> layout(int2 size) override;
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
struct HBox : Horizontal, Widgets {
    HBox(const ref<Widget*>& widgets, Extra main=Share, Extra side=AlignCenter):Linear(main,side),Widgets(widgets){}
    HBox(Extra main=Share, Extra side=AlignCenter):Linear(main,side){}
};
/// Vertical layout of heterogenous widgets. \sa Widgets
struct VBox : Vertical, Widgets {
    VBox(const ref<Widget*>& widgets, Extra main=Share, Extra side=AlignCenter):Linear(main,side),Widgets(widgets){}
    VBox(Extra main=Share, Extra side=AlignCenter):Linear(main,side){}
};
/// Horizontal layout of homogenous items. \sa Array
template<class T> struct HList : Horizontal, Array<T> {
    HList(const mref<T>& widgets):Array<T>(widgets){}
    //HList(array<T>&& widgets):Array<T>(move(widgets)){}
    HList(Extra main=Share, Extra side=AlignCenter):Linear(main,side){}
};
/// Vertical layout of homogenous items. \sa Array
template<class T> struct VList : Vertical, Array<T> {
    VList(array<T>&& widgets):Array<T>(move(widgets)){}
    VList(Extra main=Share, Extra side=AlignCenter):Linear(main,side){}
};
