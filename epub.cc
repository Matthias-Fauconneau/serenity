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
        //log(slice(readFile(arguments.first()),0,100));
        log(parseXML(move(readZip(readFile(arguments.first())).at("content.opf"_))));
    }
};
Application(EPub)
