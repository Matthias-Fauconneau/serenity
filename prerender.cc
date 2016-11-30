#include "render.h"

struct RenderApp : Render {
    RenderApp() {
        clear();
        step();
    }
} render;
