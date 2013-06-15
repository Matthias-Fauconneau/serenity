#pragma once
/// \file interface.h %Widgets (ScrollArea, ImageView, TriggerButton, ToggleButton, Progress, Slider, Item, ::TabBar)
#include "function.h"
#include "image.h"
#include "display.h"
#include "widget.h"
#include "layout.h"
#include "text.h"

/// Implements a scrollable area for \a widget
struct ScrollArea : Widget {
    /// Overrides \a widget to return the proxied widget
    virtual Widget& widget() =0;
    /// Ensures \a target is visible inside the region of the viewport
    void ensureVisible(Rect target);
    /// Centers \a target in the viewport
    void center(int2 target);
    /// Directions (false: expand, true: scroll)
    bool horizontal=false, vertical=true;
    int2 delta=0, dragStart, flickStart;

    int2 sizeHint() { return widget().sizeHint(); }
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
    void render(int2 position, int2 size) override;
    int2 size; // keep last size for ensureVisible
};

/// Makes a widget scrollable by proxying it through \a ScrollArea
template<class T> struct Scroll : ScrollArea, T {
#if __clang__ || __GNUC_MINOR__ < 8
    template<class... Args> Scroll(Args&&... args):T(forward<Args>(args)...){}
#else
    using T::T;
#endif
    /// Returns a reference to \a T::Widget (for ScrollArea implementation)
    Widget& widget() override { return (T&)*this; }
    /// Returns a reference to \a ScrollArea::Widget (e.g to add the area to a layout)
    Widget& area() { return (ScrollArea&)*this; }
};

/// Displays an image
struct ImageView : Widget {
    /// Displayed image
    Image image;

    ImageView(){}
    /// Creates a widget displaying \a image
    ImageView(Image&& image):image(move(image)){}

    int2 sizeHint();
    void render(int2 position, int2 size) override;
};
/// \typedef ImageView Icon
/// Displays an icon
typedef ImageView Icon;

/// Clickable image
struct ImageLink : ImageView {
    /// Argument given to triggered
    String link;
    /// User clicked on the image
    signal<> triggered;
    /// User clicked on the image
    signal<const string&> linkActivated;

    ImageLink(){}
    ImageLink(Image&& image):Icon(move(image)){}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
};
/// \typedef ImageLink TriggerButton
/// Displays a clickable button with an icon
typedef ImageLink TriggerButton;

/// Togglable Icon
struct ToggleButton : Widget {
    /// Creates a toggle button showing \a enable icon when disabled or \a disable icon when enabled
    ToggleButton(Image&& enable, Image&& disable) : enableIcon(move(enable)), disableIcon(move(disable)) {}

    /// User toggled the button
    signal<bool /*state*/> toggled;

    /// Current button state
    bool enabled = false;

    int2 sizeHint();
    void render(int2 position, int2 size) override;
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;

    Image enableIcon;
    Image disableIcon;
};

/// Shows a bounded value
struct Progress : Widget {
    int minimum, maximum;
    /// current \a value shown by the progress bar
    int value;

    Progress(int minimum=0, int maximum=0, int value=-1):minimum(minimum),maximum(maximum),value(value){}

    int2 sizeHint();
    void render(int2 position, int2 size) override;

    static constexpr int height = 32;
};

/// Shows and controls a bounded value
struct Slider : Progress {
    /// User edited the \a value
    signal<int> valueChanged;

    Slider(int minimum=0, int maximum=0, int value=-1):Progress(minimum,maximum,value){}

    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
};

/// ::Icon with \ref Text "text"
struct Item : Horizontal {
    Icon icon; Text text;
    Item(){}
    Item(Image&& icon, String&& text, int size=16):icon(move(icon)),text(move(text),size){}
    Widget& at(int i) override { return i==0?(Widget&)icon:(Widget&)text; }
    uint count() const override { return 2; }
};

/// Clickable Item
struct TriggerItem : Item {
    TriggerItem(){}
    TriggerItem(Image&& icon, String&& text, int size=16):Item(move(icon),move(text),size){}
    /// User clicked on the button
    signal<> triggered;
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
};

/// Bar of \link Item items\endlink
typedef Bar<Item> TabBar;
