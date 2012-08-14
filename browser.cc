#include "process.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "window.h"
#include "interface.h"

struct Browser : Application {
    Scroll<HTML> page;
    Window window i({&page.parent(),int2(0,0)});

    Browser() {
        window.localShortcut(Escape).connect(this, &Application::quit);
        page.contentChanged.connect(&window, &Window::update);
        page.go("http://www.thedreamercomic.com/issues/issue_15/05_Issue_15.jpg"_);
        window.show();
    }
};
Application(Browser)
