#include "process.h"
#include "file.h"
#include "zip.h"
#include "xml.h"
//#include "interface.h"
//#include "window.h"

struct EPub : Application {
    //Scroll<Text> content;
    //Window window{&content.parent()};

    EPub(array<string>&& arguments) {
        //window.localShortcut("Escape"_).connect(this, &Application::quit);
        //window.show();
        auto zip = mapFile(arguments.first());
        auto files = readZip(array<byte>(zip));
        Element package = parseXML(files.at("content.opf"_))("package"_);
        map<string, string> itemMap;
        for(const auto& item: package("manifest"_).children) itemMap.insert(item->at("id"_), item->at("href"_));
        array<string> items; for(const auto& itemref: package("spine"_).children) items << itemMap.at(itemref->at("idref"_));
        log(items);
    }
};
Application(EPub)
