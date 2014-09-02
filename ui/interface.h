#pragma once
/// \file interface.h %Widgets (ScrollArea, ImageView, TriggerButton, ToggleButton, Progress, Slider, Item, ::TabBar)
#include "widget.h"
#include "function.h"

// Scroll

/// Implements a scrollable area for \a widget
struct ScrollArea : Widget {
    /// Directions (false: expand, true: scroll)
    bool horizontal=false, vertical=true;
    bool scrollbar = false;
    const int scrollBarWidth = 16;
    int2 offset=0;
    int2 dragStartCursor, dragStartDelta;
    //int2 size;

    /// Overrides \a widget to return the proxied widget
    virtual Widget& widget() const abstract;

    int2 sizeHint(int2 size) const override { return widget().sizeHint(size); }
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override;
    //bool keyPress(Key key, Modifiers modifiers) override;
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
    Widget* operator&() { return &area(); }
};

// Interface

/// Shows a bounded value
struct Progress : Widget {
    int minimum, maximum;
    /// current \a value shown by the progress bar
    int value;

    Progress(int minimum=0, int maximum=0, int value=-1):minimum(minimum),maximum(maximum),value(value){}

    int2 sizeHint(int2) const override;
    Graphics graphics(int2 size) const override;

    static constexpr int height = 32;
};

/// Displays an image
struct ImageWidget : virtual Widget {
    /// Displayed image
    Image image;

    /// Creates a widget displaying \a image
    ImageWidget(Image&& image) : image(move(image)) {}

    int2 sizeHint(int2) const override;
    Graphics graphics(int2 size) const override;
};

// Control

/// Shows and controls a bounded value
struct Slider : Progress {
    /// User edited the \a value
    function<void(int)> valueChanged;

    Slider(int minimum=0, int maximum=0, int value=-1):Progress(minimum,maximum,value){}

    bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override;
};

/// Displays an icon. When clicked, calls \a triggered
struct ImageLink : ImageWidget {
    /// User clicked on the image
    function<void()> triggered;

    ImageLink(Image&& image) : ImageWidget(move(image)) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override;
};
/// \typedef ImageLink TriggerButton
/// Displays a clickable button with an icon
typedef ImageLink TriggerButton;

/// Displays an icon corresponding to state. When clicked, switches state and calls \a toggled
struct ToggleButton : ImageWidget {
    /// Creates a toggle button showing \a enable icon when disabled or \a disable icon when enabled
    ToggleButton(Image&& enable, Image&& disable) : ImageWidget(share(enable)), enableIcon(move(enable)), disableIcon(move(disable)) {}

    /// User toggled the button
    function<void(bool state)> toggled;

    /// Current button state
    bool enabled = false;

    bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override;

    Image enableIcon;
    Image disableIcon;
};
