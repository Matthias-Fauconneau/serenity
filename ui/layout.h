#pragma once
/// \file layout.h Widget layouts (Linear, Grid)
#include "widget.h"
#include "function.h"

/// Proxy Widget containing multiple widgets.
struct Layout : Widget {
	/// Derived classes should override \a count and \a at to implement widgets storage. \sa Widgets WidgetArray Tuple
	virtual size_t count() const abstract;
	virtual Widget& at(size_t) const abstract;

    /// Computes widgets layout
    virtual buffer<Rect> layout(vec2 size) abstract;
    virtual float stop(vec2 size, int unused axis, float currentPosition, int direction=0) override;

    /// Renders all visible child widgets
    shared<Graphics> graphics(vec2 size, Rect clip) override;
    /// Forwards event to intersecting child widgets until accepted
    bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus) override;
};

/// Implements Layout storage using array<Widget*> (i.e by reference)
/// \note It allows a layout to contain heterogenous Widget objects.
struct Widgets : virtual Layout, array<Widget*> {
	using array::array;
	size_t count() const override { return array::size; }
	Widget& at(size_t i) const override { return *array::at(i); }
};

/// Implements Layout storage using buffer<T> (i.e by value)
/// \note It allows a layout to directly contain homogenous items without managing pointers.
generic struct WidgetArray : virtual Layout, array<T> {
	using array<T>::array;
	WidgetArray() {}
	WidgetArray(array<T>&& items) : array<T>(move(items)){}
	size_t count() const override { return array<T>::size; }
 Widget& at(size_t i) const override { return array<T>::at(i); }
};

/// Layouts widgets on an axis
/// \note This is an abstract class, use \a Horizontal or \a Vertical
struct Linear : virtual Layout {
    default_move(Linear);

    /// How to use any extra space when no widget is expanding
    enum Extra {
        Left,Top=Left, /// Aligns tightly packed widgets
        Right,Bottom=Right, /// Aligns tightly packed widgets
        Center, /// Aligns tightly packed widgets
        Even, /// Shares space evenly
        Spread, /// Allocates minimum sizes and spreads any extra space between widgets
        Share, /// Allocates minimum sizes and shares any extra space between expanding widgets
        ShareTight, /// Allocates minimum sizes and shares any extra space between expanding widgets and margin
        AlignLeft,AlignTop=AlignLeft, /// For side axis, sets all widgets side size to maximum needed and aligns left/top
        AlignRight,AlignBottom=AlignRight, /// For side axis, sets all widgets side size to maximum needed and aligns right/bottom
        AlignCenter, /// For side axis, sets all widgets side size to largest hint (or total available if any widget is expanding) and centers
        Expand /// For side axis, sets all widgets side size to layout available side size
    };
    Extra main, side;
    /// Expands main axis even when no widget is expanding
    bool expanding;

    /// Constructs a linear layout
    /// \note This constructor should be used in most derived class (any initialization in derived classes are ignored)
    Linear(Extra main=Share, Extra side=AlignCenter, bool expanding=false) : main(main), side(side), expanding(expanding) {}

    vec2 sizeHint(vec2) override;
    buffer<Rect> layout(vec2 size) override;
    /// Transforms coordinates so that x/y always means main/side (i.e along/across) axis to reuse same code in Vertical/Horizontal
    virtual vec2 xy(vec2 xy) const abstract;
};

/// Layouts widgets on the horizontal axis
struct Horizontal : virtual Linear {
    vec2 xy(vec2 xy) const override { return xy; }
};
/// Layouts widgets on the vertical axis
struct Vertical : virtual Linear {
    vec2 xy(vec2 xy) const override { return vec2(xy.y,xy.x); }
};

/// Horizontal layout of heterogenous widgets. \sa Widgets
struct HBox : Horizontal, Widgets {
    /// Warning: As virtual Linear will be constructed by the most derived class, the layout parameter here will be ignored if HBox is not most derived
    HBox(Extra main=Share, Extra side=AlignCenter, bool expanding=false) : Linear(main, side, expanding){}
    /// Warning: As virtual Linear will be constructed by the most derived class, the layout parameter here will be ignored if HBox is not most derived
	HBox(array<Widget*>&& widgets, Extra main=Share, Extra side=AlignCenter, bool expanding=false)
		: Linear(main, side, expanding), Widgets(::move(widgets)){}
	/// Warning: As virtual Linear will be constructed by the most derived class, the layout parameter here will be ignored if HBox is not most derived
    HBox(ref<Widget*>&& widgets, Extra main=Share, Extra side=AlignCenter, bool expanding=false)
		: Linear(main, side, expanding), Widgets(copyRef(widgets)){}
};

/// Vertical layout of heterogenous widgets. \sa Widgets
struct VBox : Vertical, Widgets {
    default_move(VBox);
    /// Warning: As virtual Linear will be constructed by the most derived class, the layout parameter here will be ignored if VBox is not most derived
    VBox(Extra main=Share, Extra side=AlignCenter, bool expanding=false) : Linear(main, side, expanding) {}
    /// Warning: As virtual Linear will be constructed by the most derived class, the layout parameter here will be ignored if VBox is not most derived
	VBox(array<Widget*>&& widgets, Extra main=Share, Extra side=AlignCenter, bool expanding=false)
		: Linear(main, side, expanding), Widgets(::move(widgets)) {}
	/// Warning: As virtual Linear will be constructed by the most derived class, the layout parameter here will be ignored if VBox is not most derived
    VBox(ref<Widget*>&& widgets, Extra main=Share, Extra side=AlignCenter, bool expanding=false)
		: Linear(main, side, expanding), Widgets(copyRef(widgets)) {}
};

/// Horizontal layout of homogenous items. \sa WidgetArray
generic struct HList : Horizontal, WidgetArray<T> {
	HList(buffer<T>&& widgets) : WidgetArray<T>(move(widgets)){}
    HList(Extra main=Share, Extra side=AlignCenter):Linear(main,side){}
};

/// Vertical layout of homogenous items. \sa WidgetArray
generic struct VList : Vertical, WidgetArray<T> {
	VList(buffer<T>&& widgets) : WidgetArray<T>(move(widgets)){}
 VList(Extra main=Share, Extra side=AlignCenter):Linear(main,side){}
 VList(size_t count) {
  array<T>::grow(count);
  buffer<T>::clear();
 }
};

/// Layouts items on a #width x #height grid
struct GridLayout : virtual Layout {
    /// Whether the column widths / row heights are uniform
    const bool uniformX, uniformY;
    /// Horizontal element count, 0 means automatic
    const uint width;

    GridLayout(bool uniformX=false, bool uniformY=false, int width=0)
        : uniformX(uniformX), uniformY(uniformY), width(width) {}
    buffer<Rect> layout(vec2 size, vec2& sizeHint);
    vec2 sizeHint(vec2 size) override { vec2 sizeHint; layout(size, sizeHint); return sizeHint; }
    buffer<Rect> layout(vec2 size) override { vec2 sizeHint; return layout(size, sizeHint); }
};

/// Grid of heterogenous widgets. \sa Widgets
struct WidgetGrid : GridLayout, Widgets {
    WidgetGrid(bool uniformX=false, bool uniformY=false, int width=0)
        : GridLayout(uniformX, uniformY, width) {}
	WidgetGrid(array<Widget*>&& widgets, bool uniformX=false, bool uniformY=false, int width=0)
		: GridLayout(uniformX, uniformY, width), Widgets(::move(widgets)) {}
};

generic struct UniformGrid : GridLayout,  WidgetArray<T> {
	UniformGrid(buffer<T>&& widgets, bool uniformX=false, bool uniformY=false)
		: GridLayout(uniformX, uniformY), WidgetArray<T>(move(widgets)){}
 UniformGrid(size_t count, bool uniformX=false, bool uniformY=false)
                              : GridLayout(uniformX, uniformY) {
                              array<T>::grow(count);
                              buffer<T>::clear();
                              }
};
