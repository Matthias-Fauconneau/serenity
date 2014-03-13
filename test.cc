#include "window.h"
#include "text.h"

struct WindowTest {
    Text text {"Hello World !"_};
    Window window {&text, int2(-1), "Test"_};
    WindowTest() { window.show(); }
} test;
