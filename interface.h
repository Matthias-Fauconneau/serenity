#pragma once
#include "signal.h"
#include "image.h"
#include "layout.h"
#include "text.h"

/// Space is a proxy Widget to add space as needed
struct Space : Widget {
    int2 size=int2(-1,-1);
    int2 sizeHint() { return size; }
    void render(int2) {};
};
extern Space space; // same dummy instance can be reused

/// Scroll is a proxy Widget containing a widget in a scrollable area.
//TODO: flick, scroll indicator, scrollbar
struct ScrollArea : Widget {
    /// Override \a widget to return the proxied widget
    virtual Widget& widget() =0;
    /// Ensures \a target is visible inside the region of the viewport
    /// \note Assumes \a target is a direct child of the proxied \a widget
    void ensureVisible(Widget& target);
    /// Directions (false: expand, true: scroll)
    bool horizontal=false, vertical=true;

    int2 sizeHint() { return widget().sizeHint(); }
    void update() override;
    bool mouseEvent(int2 position, Event event, Button button) override;
    void render(int2 parent) { return widget().render(parent+position); }
};

/// Scroll<T> implement a scrollable \a T by proxying it through \a ScrollArea
/// It allows an object to act both as the content (T) and the container (ScrollArea)
/// \note \a ScrollArea and \a T will be instancied as separate Widget (non-virtual multiple inheritance)
template<class T> struct Scroll : ScrollArea, T {
    Widget& widget() override { return (T&)*this; }
    /// Return a reference to \a ScrollArea::Widget. e.g used when adding this widget to a layout
    Widget& parent() { return (ScrollArea&)*this; }
};

/// ImageView is a widget displaying a static image
struct ImageView : Widget {
    /// Displayed image
    Image<byte4> image;

    ImageView(){}
    /// Create a widget displaying \a image
    ImageView(Image<byte4>&& image):image(move(image)){}

    int2 sizeHint();
    void render(int2 parent);
};
typedef ImageView Icon;

/// TriggerButton is a clickable Icon
struct TriggerButton : Icon {
    //using Icon::Icon;
    TriggerButton(){}
    TriggerButton(Image<byte4>&& image):Icon(move(image)){}
    /// User clicked on the button
    signal<> triggered;
    bool mouseEvent(int2 position, Event event, Button button) override;
};

/// ToggleButton is a togglable Icon
struct ToggleButton : Widget {
    /// Create a toggle button showing \a enable icon when disabled or \a disable icon when enabled
    ToggleButton(const Image<byte4>& enable,const Image<byte4>& disable);

    /// User toggled the button
    signal<bool /*nextState*/> toggled;

    /// Current button state
    bool enabled = false;

    int2 sizeHint();
    void render(int2 parent);
    bool mouseEvent(int2 position, Event event, Button button) override;

    const Image<byte4>& enableIcon;
    const Image<byte4>& disableIcon;
    static const int size = 32;
};

/// Slider is a Widget to show or control a bounded value
struct Slider : Widget {
    /// \a minimum and \a maximum displayable/settable value
    int minimum = 0, maximum = 0;
    /// current \a value shown by the slider
    int value = -1;
    /// User edited the \a value
    signal<int> valueChanged;

    int2 sizeHint();
    void render(int2 parent);
    bool mouseEvent(int2 position, Event event, Button button) override;

    static const int height = 32;
};

/// Declare a small .png icon embedded in the binary, decoded on startup and accessible at runtime as an Image
/// \note an icon with the same name must be linked by the build system
///       'ld -r -b binary -o name.o name.png' can be used to embed a file in the binary
#define ICON(name) \
    extern byte _binary_icons_## name ##_png_start[]; \
    extern byte _binary_icons_## name ##_png_end[]; \
    static Image<byte4> name ## Icon = decodeImage(array<byte>(_binary_icons_## name ##_png_start,_binary_icons_## name ##_png_end-_binary_icons_## name ##_png_start))


/// Item is an icon with text
struct Item : Horizontal, Tuple<Icon,Text,Space> {
    Item():Tuple(){}
    Item(Icon&& icon, Text&& text):Tuple(move(icon),move(text),Space()){}
};

/// TabBar is a \a Bar containing \a Item elements
typedef Bar<Item> TabBar;
