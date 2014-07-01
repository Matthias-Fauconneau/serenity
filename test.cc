#include "widget.h"
#include "window.h"
#include "time.h"

struct VSyncTest : Widget {
    Window window{this,int2(512,512),"VSync"_};
    VSyncTest(){ window.actions[Escape]=[]{exit();}; window.background=NoBackground; window.show(); }
    uint count=0;
    Time misc;
    void render(const Image& target) {
        misc.start();
        fill(target, Rect(target.size()), (count++)%2?black:white);
        window.render();
        if(count>100) { misc.stop(); exit(); }
    }
    ~VSyncTest() { assert_(misc); log(count, misc, count/misc.toFloat()); }
} test;
