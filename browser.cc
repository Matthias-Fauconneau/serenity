#include "process.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "window.h"
#include "interface.h"
#include "array.cc"

struct Browser : Application {
    Scroll<HTML> content;
    Window window = Window(&content.parent(),int2(0,0));

    Browser(array< ref<byte> >&& arguments) {
        assert(arguments,"Usage: browser <url>");
        window.localShortcut(Key::Escape).connect(this, &Application::quit);
        content.contentChanged.connect(&window, &Window::render);
        content.go(arguments.first());
        window.show();
    }
};
Application(Browser)
