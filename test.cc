#include "thread.h"

#if 1
#include "string.h"
#include "time.h"
struct Test {
    Test() {
        Date start(10,April,2013), now = currentTime(), end(21,August,2013);
        uint since = now - start, until = end - now, total = since+until;
        log(since/60/60/24, until/60/60/24, total/60/60/24, 100.0*since/total, 100.0*until/total, 100.0*(24*60*60)/total);
        log(Date(start + 1*total/2));
        log(Date(start + 1*total/3), Date(start + 2*total/3));
    }
} test;
#endif

#if 0
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
