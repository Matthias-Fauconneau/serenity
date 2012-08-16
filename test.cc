#include "window.h"
#include "text.h"
struct Test : Application{
    Text text __(string("Hello World!"_));
    Window window __(&text);
    Test(){ window.show(); }
};
Application(Test)
