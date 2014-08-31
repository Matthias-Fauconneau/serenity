#pragma once
/// \file interface.h %Widgets (ScrollArea, ImageView, TriggerButton, ToggleButton, Progress, Slider, Item, ::TabBar)
#include "function.h"
#include "image.h"
#include "widget.h"
#include "layout.h"
#include "text.h"

/// Implements a scrollable area for \a widget
struct ScrollArea : Widget {
    /// Directions (false: expand, true: scroll)
    bool horizontal=false, vertical=true;
    bool scrollbar = false;
    const int scrollBarWidth = 16;
    int2 offset=0;
    int2 dragStartCursor, dragStartDelta;
    int2 size; // Keeps last size for ensureVisible

    /// Overrides \a widget to return the proxied widget
    virtual Widget& widget() const abstract;
    /// Ensures \a target is visible inside the region of the viewport
    void ensureVisible(Rect target);
    /// Centers \a target in the viewport
    void center(int2 target);

    int2 sizeHint(int2 size) { return widget().sizeHint(size); }
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
    bool keyPress(Key key, Modifiers modifiers) override;
    Graphics graphics(int2 size) const override;
};

/// Makes a widget scrollable by proxying it through \a ScrollArea
template<class T> struct Scroll : ScrollArea, T {
    using T::T;
    /// Returns a reference to \a T::Widget (for ScrollArea implementation)
    Widget& widget() const override { return (T&)*this; }
    /// Returns a reference to \a ScrollArea::Widget (e.g to add the area to a layout)
    Widget& area() { return (ScrollArea&)*this; }
    /// Returns a reference to \a ScrollArea::Widget (e.g to add the area to a layout)
    Widget& operator&() { return area(); }
};

/// Displays an image
struct ImageWidget : virtual Widget {
    /// Displayed image
    const Image& image;
    /// Hides image
    //bool hidden = false;

    /// Creates a widget displaying \a image
    ImageWidget(const Image& image/*, bool hidden=false*/) : image(move(image))/*, hidden(hidden)*/ {}

    int2 sizeHint() const override;
    Graphics graphics(int2 size) const override;
};

/// \typedef ImageView Icon
/// Displays an icon
typedef ImageWidget Icon;
