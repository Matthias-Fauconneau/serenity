#include "process.h"
#include "linux.h"
#include "debug.h"
#include "font.h"
//#include "display.h"
//#include "text.h"
/*#include "zip.h"
#include "xml.h"
//#include "html.h"
//#include "interface.h"
//#include "window.h"

struct EPub : Application {
    //Scroll<Text> content;
    //Window window{&content.parent()};

    EPub(array<string>&& arguments) {
        //window.localShortcut("Escape"_).connect(this, &Application::quit);
        //window.show();
        Map zip = mapFile(arguments.first());
        map<string, ZipFile> files = readZip(array<byte>(zip));
        Element content = parseXML(files.at("content.opf"_));
        const Element& package = content("package"_);
        map<string, string> itemMap;
        for(const auto& item: package("manifest"_).children) itemMap.insert(item.at("id"_), item.at("href"_));
        array<string> items; for(const auto& itemref: package("spine"_).children) items << copy(itemMap.at(itemref.at("idref"_)));
        for(const string& item: items) {
            log(parseHTML(files.at(item)));
            break;
        }
    }
};
Application(EPub)*/

struct Test : Application {
   // Text text {"Hello World!"_};
    Test(array<string>&&) {
        catchErrors();
        Font font("truetype/ttf-dejavu/DejaVuSans.ttf"_);
        log(font.index('H'));
        //openDisplay();
        //text.position = int2(500,100); text.Widget::size = text.sizeHint();
        //text.render(int2(0,0));
    }
};
Application(Test)

