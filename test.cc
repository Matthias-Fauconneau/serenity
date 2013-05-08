#include "process.h"

#if 0
#include "string.h"
struct Test {
    Test() {
        log("Hello World !");
    }
} test;
#endif

#if 1
#include "widget.h"
#include "display.h"
#include "window.h"
#include "time.h"
struct VSyncTest : Widget {
    Window window{this,int2(512,512),"VSync"_};
    VSyncTest(){ window.localShortcut(Escape).connect(&exit); window.clearBackground=false; window.show(); }
    uint count=0; void render(int2 position, int2 size) { static Time misc; log((uint64)misc); /*fill(position+Rect(size),((count++)%2)?black:white);*/ window.render(); if(count>100) exit(); misc.reset(); }
} test;
#endif
