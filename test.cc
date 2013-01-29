#include "window.h"
#include "display.h"
#include "jpeg.h"

struct GLTest : Widget {
    Window window  __(this, int2(1050,1250), "GLTest"_, Window::OpenGL);
    GLTest() {
        window.localShortcut(Escape).connect(&::exit);
    }
    void render(int2 position, int2 size) {
        static Image image = decodeImage(readFile("Island/textures/Skymap_offworld_gen2.jpg"_,home()));
        blit(position, image);
    }
} test;
