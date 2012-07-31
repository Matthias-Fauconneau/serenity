#include "process.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "window.h"
#include "interface.h"

struct Browser : Application {
    Scroll<HTML> page;
    Window window = Window(&content.parent(),int2(0,0));

    Browser() {
        window.localShortcut(Key::Escape).connect(this, &Application::quit);
        page.contentChanged.connect(&window, &Window::render);
        page.go("http://thedreamercomic.com/blog.php/page-3-fighting-cocks-tavern/"_);
        window.show();
    }
};
Application(Browser)
