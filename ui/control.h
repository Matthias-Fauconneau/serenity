#pragma once
#include "interface.h"

/// Clickable image
struct ImageLink : ImageWidget {
    /// Argument given to triggered
    String link;
    /// User clicked on the image
    function<void()> triggered;
    /// User clicked on the image
    signal<const string&> linkActivated;

    ImageLink(const Image& image, bool hidden=false) : ImageWidget(move(image),hidden){}
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
    function<void(bool state)> toggled;

    /// Current button state
    bool enabled = false;

    int2 sizeHint();
    Graphics graphics(int2 size) override;
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
    void render() override;

    static constexpr int height = 32;
};

/// Shows and controls a bounded value
struct Slider : Progress {
    /// User edited the \a value
    function<void(int)> valueChanged;

    Slider(int minimum=0, int maximum=0, int value=-1):Progress(minimum,maximum,value){}

    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
};

/// ::Icon with \ref Text "text"
struct Item : Linear {
    Item(Image&& icon, const string& text, int size=16, bool under=false) : icon(move(icon)), text(text,size), under(under){}
    Widget& at(int i) override { return i==0?(Widget&)icon:(Widget&)text; }
    uint count() const override { return 2; }
    int2 xy(int2 xy) override { return under ? int2(xy.y,xy.x) : xy /*right*/; }

    Icon icon;
    Text text;
    bool under=false; // Displays text label under icon (instead of inline)
};

/// Clickable Item
struct TriggerItem : Item {
    TriggerItem(Image&& icon, String&& text, int size=16):Item(move(icon),move(text),size){}
    /// User clicked on the button
    signal<> triggered;
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
};

/// Bar of \link Item items\endlink
typedef Bar<Item> TabBar;
