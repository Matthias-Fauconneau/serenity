#include "process.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "interface.h"
//#include "window.h"

struct Browser : Application {
    Scroll<HTML> content;
    //Window window{&content.parent()};

    Browser(array<string>&& arguments) {
        //window.localShortcut("Escape"_).connect(this, &Browser::quit);
        content.contentChanged.connect(this, &Browser::render);
        content.go(arguments.first());
        //window.show();
    }
    void render() {
        //if(window.visible) { content.widget().update(); window.render(); }
        content.widget().update(); content.widget().render();
    }
};
Application(Browser)
