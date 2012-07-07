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
        window.show();
        content.go(arguments.first());
    }
    void render() { if(window.visible) { window.widget->update(); window.render(); } }
};
Application(Browser)

/*#include "http.h"
struct Test : Application {
    void handler(const URL&, array<byte>&& data){ log(data.size()); }
    Test(array<string>&&) {
        getURL("google.com"_, Handler(this, &Test::handler), 60);
    }
};
Application(Test)*/
