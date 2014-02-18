#include "widget.h"

Image renderToImage(Widget& widget, int2 size) {
    Image target(size.x, size.y);
    for(uint y: range(size.y)) for(uint x: range(size.x)) target.data[y*target.stride+x] = 0xFF;
    assert(&widget);
    widget.render(target);
    return target;
}
