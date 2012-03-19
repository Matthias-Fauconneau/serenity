#include "process.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "array.cc"

struct Browser : Application {
    void start(array<string>&& arguments) {
        array<string> feeds = apply<string>( parseXML(readFile(arguments.first())).xpath("opml/body/outline/outline"_),
                                             [](const Element& e){return e["xmlUrl"_];});
    for(const string& url: feeds) {
        Element document = parseXML( HTTP::getURL(url) );
        const Element& feed = document("feed"_).first();
        log(feed["title"_]);
        break;
    }
}
} browser;
