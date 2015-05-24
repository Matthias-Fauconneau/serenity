#include "thread.h"
#include "window.h"
#include "gl.h"

struct Isometric : Widget {
    unique<Window> window = ::window(this, 1050);
    vec2 sizeHint(vec2) { return vec2(1050); }
    shared<Graphics> graphics(vec2 unused size) {
        Image target = Image(int2(size));
        target.clear(0xFF);
        shared<Graphics> graphics;
        graphics->blits.append(0, size, move(target));
        return graphics;
    }
} view;
