#include "window.h"
#include "display.h"
#include "text.h"
#include "html.h"

struct VSyncTest : Application, Widget {
    Window window __(this,int2(0,0),"VSyncTest"_);
    VSyncTest(){ window.localShortcut(Escape).connect(this,&Application::quit); window.show(); }
    void render(int2 position, int2 size) {static bool odd; fill(position+Rect(size),(odd=!odd)?black:white); window.render();}
};

struct FontTest : Application {
    Text text __(string("The quick brown fox jumps over the lazy dog\nI know that a lot of you are passionate about the civil war\nLa Poste Mobile a gagné 4000 clients en six mois"_));
    Window window __(&text,int2(-1,-1),"Font Test"_);
    FontTest(){ window.localShortcut(Escape).connect(this,&Application::quit); window.show(); }
};

struct KeyTest : Application, Text {
    Window window __(this,int2(640,480),"KeyTest"_);
    KeyTest(){ window.globalShortcut(Play).connect(this,&Application::quit); focus=this; window.localShortcut(Escape).connect(this,&Application::quit); window.show(); }
    bool keyPress(Key key) { setText(str(dec(int(key)),hex(int(key)))); return true; }
};

struct HTMLTest : Application {
    Scroll<HTML> page;
    Window window __(&page.area(),int2(0,0),"Browser"_);

    HTMLTest() {
        window.localShortcut(Escape).connect(this, &Application::quit);
        page.contentChanged.connect(&window, &Window::render);
        page.go("http://www.tryinghuman.com/?id=607"_);
        window.show();
    }
};

Application(KeyTest)
