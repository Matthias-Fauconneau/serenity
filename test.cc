#include "window.h"
#include "time.h"

struct VSyncTest : Widget {
    Window window{this,int2(512,512),"VSync"_};
    function<void()> onGraphics;
    uint frameCount=0;
    Time totalTime;
    VSyncTest(){
        window.background=Window::Black;
        onGraphics = [&]{
            totalTime.start();
            if(frameCount>=100) { totalTime.stop(); exit(); }
            else {
                window.background = (frameCount++)%2 ? Window::Black : Window::White;
                window.render();
            }
        };
    }
    int2 sizeHint(int2) const { return 0; }
    Graphics graphics(int2 size) const override {
        Graphics graphics;
        graphics.fills << Fill{vec2(size/4), vec2(size/2), window.background==Window::Black?1:0, 1};
        onGraphics(); // Works around const restriction to increment frame counter
        return graphics;
    }
    ~VSyncTest() { assert_(totalTime); log(frameCount, totalTime, frameCount/totalTime.toFloat()); }
} test;
