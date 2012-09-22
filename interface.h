#pragma once
#include "function.h"
#include "image.h"
#include "display.h"
#include "widget.h"
#include "layout.h"
#include "text.h"

/// Scroll is a proxy Widget containing a widget in a scrollable area.
//TODO: flick, scroll indicator, scrollbar
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
    bool mouseEvent(int2 cursor, int2 size, Event event, MouseButton button) override;
    void render(int2 position, int2 size) override;
    int2 size; // keep last size for ensureVisible
};

/// Scroll<T> implements a scrollable \a T by proxying it through \a ScrollArea
/// It allows an object to act both as the content (T) and the container (ScrollArea)
/// \note \a ScrollArea and \a T will be instancied as separate Widget (non-virtual multiple inheritance)
template<class T> struct Scroll : ScrollArea, T {
    template<class... Args> Scroll(Args&&... args):T(forward<Args>(args)___){}
    Widget& widget() override { return (T&)*this; }
    /// Returns a reference to \a ScrollArea::Widget. e.g used when adding this widget to a layout
    Widget& area() { return (ScrollArea&)*this; }
};

/// ImageView is a widget displaying a static image
struct ImageView : Widget {
    /// Displayed image
    Image image;

    ImageView(){}
    /// Creates a widget displaying \a image
    ImageView(Image&& image):image(move(image)){}

    int2 sizeHint();
    void render(int2 position, int2 size) override;
};
typedef ImageView Icon;

/// TriggerButton is a clickable Icon
struct TriggerButton : Icon {
    TriggerButton(){}
    TriggerButton(Image&& image):Icon(move(image)){}
    /// User clicked on the button
    signal<> triggered;
    bool mouseEvent(int2 cursor, int2 size, Event event, MouseButton button) override;
};

/// ToggleButton is a togglable Icon
struct ToggleButton : Widget {
    /// Creates a toggle button showing \a enable icon when disabled or \a disable icon when enabled
    ToggleButton(Image&& enable, Image&& disable) : enableIcon(move(enable)), disableIcon(move(disable)) {}

    /// User toggled the button
    signal<bool /*state*/> toggled;

    /// Current button state
    bool enabled = false;

    int2 sizeHint();
    void render(int2 position, int2 size) override;
    bool mouseEvent(int2 cursor, int2 size, Event event, MouseButton button) override;

    Image enableIcon;
    Image disableIcon;
};

/// Progress is a Widget to show a bounded value
struct Progress : Widget {
    int minimum, maximum;
    /// current \a value shown by the progress bar
    int value;

    Progress(int minimum=0, int maximum=0, int value=-1):minimum(minimum),maximum(maximum),value(value){}

    int2 sizeHint();
    void render(int2 position, int2 size) override;

    static const int height = 32;
};

/// Slider is a Widget to show and control a bounded value
struct Slider : Progress {
    /// User edited the \a value
    signal<int> valueChanged;

    bool mouseEvent(int2 cursor, int2 size, Event event, MouseButton button) override;
};

/// Item is an icon with text
struct Item : Horizontal {
    Icon icon; Text text;
    Item(){}
    Item(Image&& icon, string&& text, int size=16):icon(move(icon)),text(move(text),size){}
    Widget& at(int i) override { return i==0?(Widget&)icon:(Widget&)text; }
    uint count() const override { return 2; }
};

/// TriggerButton is a clickable Item
struct TriggerItem : Item {
    TriggerItem(){}
    TriggerItem(Image&& icon, string&& text, int size=16):Item(move(icon),move(text),size){}
    /// User clicked on the button
    signal<> triggered;
    bool mouseEvent(int2 cursor, int2 size, Event event, MouseButton button) override;
};

/// TabBar is a \a Bar containing \a Item elements
typedef Bar<Item> TabBar;
