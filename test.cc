#include "window.h"
#include "text.h"
struct Test : Application{
    Text text __(string("The quick brown fox jumps over the lazy dog"_));
    Window window __(&text);
    Test(){ window.localShortcut(Escape).connect(this,&Application::quit); window.show(); }
};
Application(Test)
