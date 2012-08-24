#include "process.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "window.h"
#include "interface.h"

struct Browser : Application {
    Scroll<HTML> page;
    Window window __(&page.area(),int2(0,0),"Browser"_);

    Browser() {
        window.localShortcut(Escape).connect(this, &Application::quit);
        page.contentChanged.connect(&window, &Window::render);
        page.go("http://www.phoronix.com/scan.php?page=news_item&px=MTE2NjQ"_);
        window.show();
    }
};
Application(Browser)
