#if 1
#include "time.h"

struct Test { Test() { log(Date(currentTime())); } } test;

#endif

#if 0
#include "thread.h"
#include "window.h"
#include "interface.h"

struct Test : Text  {
    Window window{this, int2(640,480), "Test"_};
    Test() : Text("Test"_) {
        window.localShortcut(Escape).connect([]{exit();});
        window.show();
    }
} test;
#endif
