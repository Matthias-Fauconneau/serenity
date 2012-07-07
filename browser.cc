#include "process.h"
/*#include "file.h"
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
        content.widget().update(); content.widget().render(int2(0,16));
    }
};
Application(Browser)
*/
#include "http.h"
struct Test : Application {
    void handler(const URL&, array<byte>&& data){ log(data.size()); }
    Test(array<string>&&) {
        getURL("google.com"_, Handler(this, &Test::handler), 60);
    }
};
Application(Test)
