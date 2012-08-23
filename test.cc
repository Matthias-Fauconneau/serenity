#include "window.h"
#include "text.h"

struct Test : Application{
    Text text __(string("The quick brown fox jumps over the lazy dog"_),16);
    Window window __(&text,int2(-1,-1),"Test"_);
    Test(){ window.localShortcut(Escape).connect(this,&Application::quit); window.show(); }
};
Application(Test)
