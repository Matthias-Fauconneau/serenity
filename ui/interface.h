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

    int2 sizeHint(int2 size) override { return widget().sizeHint(size); }
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override;
    //bool keyPress(Key key, Modifiers modifiers) override;
    Graphics graphics(int2 size) override;
};

/// Makes a widget scrollable by proxying it through \a ScrollArea
generic struct Scroll : ScrollArea, T {
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

    int2 sizeHint(int2) override;
    Graphics graphics(int2 size) override;

    static constexpr int height = 32;
};

/// Displays an image
struct ImageView : Widget {
    /// Displayed image
    Image image;

    ImageView() {}
    /// Creates a widget displaying \a image
    ImageView(Image&& image) : image(move(image)) {}

    int2 sizeHint(int2) override;
	Graphics graphics(int2 size) override;
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
struct ImageLink : ImageView {
    /// User clicked on the image
    function<void()> triggered;

    ImageLink(Image&& image) : ImageView(move(image)) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override;
};
/// \typedef ImageLink TriggerButton
/// Displays a clickable button with an icon
typedef ImageLink TriggerButton;

/// Displays an icon corresponding to state. When clicked, switches state and calls \a toggled
struct ToggleButton : ImageView {
    /// Creates a toggle button showing \a enable icon when disabled or \a disable icon when enabled
    ToggleButton(Image&& enable, Image&& disable) : ImageView(share(enable)), enableIcon(move(enable)), disableIcon(move(disable)) {}

    /// User toggled the button
    function<void(bool state)> toggled;

    /// Current button state
    bool enabled = false;

    bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override;

    Image enableIcon;
    Image disableIcon;
};

generic buffer<Widget*> toWidgets(mref<T> widgets) { return apply(widgets, [](T& widget) -> Widget* { return &widget; }); }

/// Several widgets in one spot, cycled by user
struct WidgetCycle : Widget {
	buffer<Widget*> widgets;
	size_t index = 0;

	WidgetCycle(ref<Widget*> widgets) : widgets(widgets) {}
	//generic WidgetCycle(mref<T> widgets) : widgets(apply(widgets, [](T& widget) -> Widget* { return &widget; })) {}
	//template<Type T, size_t N> WidgetCycle(T (&a)[N]) : WidgetCycle(mref<T>(a,N)) {}

    // Forwards content
    String title() override { return widgets[index]->title(); }
    int2 sizeHint(int2 size) override { return widgets[index]->sizeHint(size); }
    Graphics graphics(int2 size) override { return widgets[index]->graphics(size); }

	// Forwards events
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override;
	// Forwards events and cycle widgets
    bool keyPress(Key key, Modifiers modifiers) override;
	bool keyRelease(Key key, Modifiers modifiers) override { return widgets[index]->keyRelease(key, modifiers); }
};

struct Index {
	size_t* pointer;
	Index(size_t* pointer) : pointer(pointer) {}
	void operator=(size_t value) { *pointer = value; }
	operator size_t() const { return *pointer; }
	operator size_t&() { return *pointer; }
};
inline String str(Index o) { return str(*o.pointer); }
