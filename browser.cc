#include "process.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "window.h"
#include "interface.h"

struct Browser : Application {
    Scroll<HTML> content;
    Window window{&content.parent(),"Browser"_,Image<byte4>(),int2(0,0)};

    Browser(array<string>&& arguments) {
        assert(arguments,"Usage: browser <url>");
        window.localShortcut(Key::Escape).connect(this, &Browser::quit);
        content.contentChanged.connect(this, &Browser::render);
        content.go(arguments.first());
        window.show();
    }
    void render() { window.render(); }
};
Application(Browser)
