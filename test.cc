#include "thread.h"
#include "window.h"
#include "text.h"

struct Test {
    TextInput input {"test"_};
    Window window {&input, int2(256,64), "Test"_};
    Test() {
        window.localShortcut(Escape).connect([]{exit(0);});
        window.show();
    }
} test;
