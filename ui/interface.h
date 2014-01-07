#pragma once
/// \file interface.h %Widgets (ScrollArea, ImageView, TriggerButton, ToggleButton, Progress, Slider, Item, ::TabBar)
#include "function.h"
#include "image.h"
#include "display.h"
#include "widget.h"
#include "layout.h"
#include "text.h"

/// Configures global display context to render to an image
// In this module because the definition depends on display and widget
Image renderToImage(Widget& widget, int2 size, int imageResolution=::resolution);

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
    bool scrollbar = false;
    const int scrollBarWidth = 16;
    int2 delta=0;
    int2 dragStartCursor, dragStartDelta;

    int2 sizeHint() { return widget().sizeHint(); }
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
    bool keyPress(Key key, Modifiers modifiers) override;
    void render(int2 position, int2 size) override;
    int2 size; // keep last size for ensureVisible
};

/// Makes a widget scrollable by proxying it through \a ScrollArea
template<class T> struct Scroll : ScrollArea, T {
    using T::T;
    /// Returns a reference to \a T::Widget (for ScrollArea implementation)
    Widget& widget() override { return (T&)*this; }
    /// Returns a reference to \a ScrollArea::Widget (e.g to add the area to a layout)
    Widget& area() { return (ScrollArea&)*this; }
};

/// Displays an image
struct ImageWidget : virtual Widget {
    /// Displayed image
    const Image& image;

    //ImageWidget(){}
    /// Creates a widget displaying \a image
    ImageWidget(const Image& image):image(move(image)){}

    int2 sizeHint();
    void render(int2 position, int2 size) override;
};
/// \typedef ImageView Icon
/// Displays an icon
typedef ImageWidget Icon;

/// Clickable image
struct ImageLink : ImageWidget {
    /// Argument given to triggered
    String link;
    /// User clicked on the image
    signal<> triggered;
    /// User clicked on the image
    signal<const string&> linkActivated;

    //ImageLink(){}
    ImageLink(const Image& image):Icon(move(image)){}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
};
/// \typedef ImageLink TriggerButton
/// Displays a clickable button with an icon
typedef ImageLink TriggerButton;

/// Togglable Icon
struct ToggleButton : Widget {
    /// Creates a toggle button showing \a enable icon when disabled or \a disable icon when enabled
    ToggleButton(const Image& enable, const Image& disable) : enableIcon(move(enable)), disableIcon(move(disable)) {}

    /// User toggled the button
    signal<bool /*state*/> toggled;

    /// Current button state
    bool enabled = false;

    int2 sizeHint();
    void render(int2 position, int2 size) override;
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;

    const Image& enableIcon;
    const Image& disableIcon;
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
struct Item : Linear {
    //Item(){}
    Item(Image&& icon, const string& text, int size=16, bool under=false):icon(move(icon)),text(text,size),under(under){}
    Widget& at(int i) override { return i==0?(Widget&)icon:(Widget&)text; }
    uint count() const override { return 2; }
    int2 xy(int2 xy) override { return under ? int2(xy.y,xy.x) : xy /*right*/; }

    Icon icon;
    Text text;
    bool under=false; // Displays text label under icon (instead of inline)
};

/// Clickable Item
struct TriggerItem : Item {
    //TriggerItem(){}
    TriggerItem(Image&& icon, String&& text, int size=16):Item(move(icon),move(text),size){}
    /// User clicked on the button
    signal<> triggered;
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
};

/// Bar of \link Item items\endlink
typedef Bar<Item> TabBar;
