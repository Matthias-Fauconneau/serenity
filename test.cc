#include "process.h"
#include "window.h"

struct WindowsTest : Widget {
    Window window  __(this, int2(1050,1050), "WindowsTest"_);
    WindowsTest() {
        window.localShortcut(Escape).connect(&::exit);
    }
    void render(int2, int2) {}
} test;
