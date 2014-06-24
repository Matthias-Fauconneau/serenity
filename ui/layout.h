#pragma once
/// \file layout.h Widget layouts (Linear, Grid)
#include "widget.h"
#include "function.h"

/// Proxy Widget containing multiple widgets.
struct Layout : Widget {
    /// Derived classes should override \a count and \a at to implement widgets storage. \sa WidgetReferences, WidgetValues
    virtual uint count() const =0;
    virtual Widget& at(int) =0;

    /// Computes widgets layout
    virtual buffer<Rect> layout(int2 size)=0;

    /// Renders all visible child widgets
    void render() override;
    /// Forwards event to intersecting child widgets until accepted
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
};

/// Implements Layout storage using buffer<Widget*> (i.e by reference)
/// \note It allows a layout to contain heterogenous Widget objects.
struct WidgetReferences : virtual Layout, buffer<Widget*> {
    WidgetReferences(){}
    WidgetReferences(const ref<Widget*>& widgets) : buffer(copy(buffer(widgets))) {}
    uint count() const override { return buffer::size; }
    Widget& at(int i)  override { return *buffer::at(i); }
};

/// Implements Layout storage using buffer<T> (i.e by value)
/// \note It allows a layout to directly contain homogenous items without managing pointers.
template<class T> struct WidgetValues : virtual Layout, buffer<T> {
    WidgetValues(){}
    WidgetValues(buffer<T>&& items) : buffer<T>(move(items)){}
    uint count() const override { return buffer<T>::size; }
    Widget& at(int i) override { return buffer<T>::at(i); }
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
    buffer<Rect> layout(int2 size) override;
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

/// Horizontal layout of heterogenous widgets. \sa WidgetReferences
struct HBox : Horizontal, WidgetReferences {
    HBox(const ref<Widget*>& widgets, Extra main=Share, Extra side=AlignCenter):Linear(main,side),WidgetReferences(widgets){}
    HBox(Extra main=Share, Extra side=AlignCenter):Linear(main,side){}
};
/// Vertical layout of heterogenous widgets. \sa WidgetReferences
struct VBox : Vertical, WidgetReferences {
    VBox(const ref<Widget*>& widgets, Extra main=Share, Extra side=AlignCenter):Linear(main,side),WidgetReferences(widgets){}
    VBox(Extra main=Share, Extra side=AlignCenter):Linear(main,side){}
};

/// Horizontal layout of homogenous items. \sa WidgetValues
template<class T> struct HList : Horizontal, WidgetValues<T> {
    HList(buffer<T>&& widgets):WidgetValues<T>(move(widgets)){}
    HList(const ref<T>& widgets):WidgetValues<T>(buffer<T>(widgets)){} // by reference
    HList(Extra main=Share, Extra side=AlignCenter):Linear(main,side){}
};
/// Vertical layout of homogenous items. \sa WidgetValues
template<class T> struct VList : Vertical, WidgetValues<T> {
    VList(buffer<T>&& widgets):WidgetValues<T>(move(widgets)){}
    VList(const ref<T>& widgets):WidgetValues<T>(buffer<T>(widgets)){} // by reference
    VList(Extra main=Share, Extra side=AlignCenter):Linear(main,side){}
};

/// Layouts items on an uniform #width x #height grid
struct GridLayout : virtual Layout {
    /// Horizontal element count, 0 means automatic
    int width;
    /// Vertical element count, 0 means automatic
    int height;
    /// Margin between elements
    int2 margin;
    GridLayout(int width=0, int height=0, int margin=0):width(width),height(height),margin(margin){}
    int2 sizeHint();
    buffer<Rect> layout(int2 size) override;
};
/// Grid of heterogenous widgets. \sa Widgets
struct WidgetGrid : GridLayout, WidgetReferences {
    WidgetGrid(){}
    WidgetGrid(buffer<Widget*>&& widgets):WidgetReferences(move(widgets)){}
};
template<class T> struct UniformGrid : GridLayout,  WidgetValues<T> {
    UniformGrid(const ref<T>& items={}, int width=0) : GridLayout(width), WidgetValues<T>(buffer<T>(items)) {}
};

struct Value {
    uint value;
    array<Widget*> widgets;
    explicit Value(uint value) : value(value) {}
    Value& registerWidget(Widget* widget) { widgets << widget; return *this; }
    void render() { for(Widget* widget: widgets) widget->render(); }
};
