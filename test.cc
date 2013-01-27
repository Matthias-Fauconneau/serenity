#include "window.h"
#include "display.h"

struct GLTest : Widget {
    Window window  __(this, int2(1050/2,1650/2), "GLTest"_, Window::OpenGL);
    GLTest() {
        window.localShortcut(Escape).connect(&::exit);
    }
    void render(int2 position, int2 size) {
        log("render",position, size);
        fill(position+Rect(size));
    }
} test;
