#include "widget.h"
#include "window.h"
#include "time.h"

#if 0
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
#else
struct Reminder : Widget {
    const uint interval = 60*1000;
    Timer timer { [this]{ window.show(); } };
    Window window {this,int2(512,512),"Reminder"_};
    Reminder() {
        window.actions[Escape]=[]{exit();}; window.background=White; window.focus=this;
        timer.setRelative(interval);
    }
    void render(const Image&) {}
    bool keyPress(Key key, Modifiers) {
        if(key>='1' && key<='9') {
            timer.setRelative((key-'0')*interval);
            window.hide();
        }
        return false;
    }
} app;
#endif
