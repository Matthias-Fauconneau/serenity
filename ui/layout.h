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
    uint count() const override { log("count", buffer<T>::size); return buffer<T>::size; }
    Widget& at(int i) override { log("at", i); return buffer<T>::at(i); }
};

/// item is an helper to instanciate a class and append the instance to the tuple offset table
template<class B, class T> struct item : T { //FIXME: static register
    void registerInstance(byte* object, uint8* list, int& i) { int d=(byte*)(B*)this-object; assert_(d>=0&&d<256); list[i++]=d; }
    item(byte* object, uint8* list, int& i) { registerInstance(object,list,i); }
    item(T&& t, byte* object, uint8* list, int& i) : T(forward<T>(t)) { registerInstance(object,list,i); }
};
/// \a tuple with static indexing by type and dynamic indexing using an offset table
template<class B, class... T> struct tuple  : item<B,T>... {
    static uint8 offsets[sizeof...(T)];
    tuple(int i=0) : item<B,T>((byte*)this, offsets,i)... {}
    tuple(int i,T&&... t) : item<B,T>(move(t), (byte*)this, offsets,i)... {}
    int size() const { return sizeof...(T); }
    template<class A> A& get() { return static_cast<A&>(*this); }
    template<class A> const A& get() const { return static_cast<const A&>(*this); }
    B& at(int i) { return *(B*)((byte*)this+offsets[i]); }
};
template<class B, class... T> uint8 tuple<B,T...>::offsets[sizeof...(T)];

/// Tuple implements Layout storage using static inheritance
/// \note It allows a layout to directly contain heterogenous Widgets without managing heap pointers.
template<class... T> struct Tuple : virtual Layout {
    tuple<Widget,T...> items;
    Tuple() : items() {}
    Tuple(T&&... t) : items(0,forward<T>(t)...) {}
    Widget& at(int i) override { return items.at(i); }
    uint count() const override { return items.size(); }
    template<class A> A& get() { return items.template get<A>(); }
    template<class A> const A& get() const { return items.template get<A>(); }
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
    //HList(const ref<T>& widgets):WidgetValues<T>(buffer<T>(widgets)){}
    HList(Extra main=Share, Extra side=AlignCenter):Linear(main,side){}
};
/// Vertical layout of homogenous items. \sa WidgetValues
template<class T> struct VList : Vertical, WidgetValues<T> {
    VList(buffer<T>&& widgets):WidgetValues<T>(move(widgets)){}
    //VList(const ref<T>& widgets):WidgetValues<T>(buffer<T>(widgets)){}
    VList(Extra main=Share, Extra side=AlignCenter):Linear(main,side){}
};

/// Horizontal layout of homogenous items. \sa WidgetValues
template<Type... T> struct HTuple : Horizontal, Tuple<T...> {
    HTuple(T&&... t) : Tuple<T...>(forward<T>(t)...) {}
};
template<Type... T> HTuple<T...> hTuple(T&&... t) { return HTuple<T...>(forward<T>(t)...); }
