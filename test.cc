#include "process.h"
#include "window.h"
#include "display.h"

struct WindowsTest : Widget {
    Window window  __(this, int2(1050,1050), "WindowsTest"_);
    WindowsTest() {
        window.localShortcut(Escape).connect(&::exit);
    }
    void render(int2 position, int2 size) {
        fill(position+Rect(size),red);
    }
} test;
