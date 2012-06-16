#include "process.h"
#include "file.h"
#include "zip.h"
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
Application(EPub)
