#include "renderer.h"

struct RenderApp : Render {
    RenderApp() {
        clear();
        step();
    }
} render;
