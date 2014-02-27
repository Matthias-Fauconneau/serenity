#include "widget.h"

// Provides weak symbols in case an application links widget without window only to render to image
__attribute((weak)) void setFocus(Widget*) { error("Window support not linked"_); }
__attribute((weak)) bool hasFocus(Widget*) { error("Window support not linked"_); }
__attribute((weak)) void setDrag(Widget*) { error("Window support not linked"_); }
__attribute((weak)) String getSelection(bool) { error("Window support not linked"_); }
__attribute((weak)) void setCursor(Rect, Cursor) { error("Window support not linked"_); }

Image renderToImage(Widget& widget, int2 size) {
    Image target(size.x, size.y);
    for(uint y: range(size.y)) for(uint x: range(size.x)) target.data[y*target.stride+x] = 0xFF;
    assert(&widget);
    widget.render(target);
    return target;
}
