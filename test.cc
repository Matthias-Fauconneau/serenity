#include "thread.h"

#if 1
#include "string.h"
#include "time.h"
struct Test {
    Test() {
        Date start(10,April,2013), now = currentTime(), end(21,August,2013);
        uint since = now - start, until = end - now, total = since+until;
        log("Elapsed:",since/60/60/24,"days", "["_+str(100.0*since/total)+"%]"_, "\tRemaining:",until/60/60/24,"days", "["_+str(100.0*until/total)+"%]"_,"\tTotal:",total/60/60/24,"days");
        log("Half",Date(start + 1*total/2));
        log("1/3", Date(start + 1*total/3), "\t2/3", Date(start + 2*total/3));
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
