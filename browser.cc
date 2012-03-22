#include "process.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "array.cc"
#include "interface.h"
#include "window.h"

struct Link : Item {
    string url;
    signal<string /*url*/> triggered;
    Link(string&& text, string&& url):Item(Image(),move(text)),url(move(url)){}
};

ICON(rss);

struct Browser : Application {
    List<Link> news;
    HBox main { &news };
    Window window{&main,"Feeds"_,copy(rssIcon),int2(-1,-1),224};
    Browser() {
        window.keyPress.connect(this, &Browser::keyPress);
    }
    void start(array<string>&& arguments) {
        //array<string> feeds = parseXML(readFile(.kde4/share/apps/akregator/data/feeds.opml)).xpath("body/outline/outline"_,"xmlUrl"_);
        array<string> feeds = split(readFile(arguments.first()),'\n');
        feeds.setSize(1); //TESTING
        for(const string& url: feeds) {
            log(url);
            Element feed = parseXML( HTTP::getURL(url) );
            bool anyNews=false;
            feed.xpath("channel/item"_,
                       [this,&anyNews,&feed](const Element& e) {
                       //TODO: check if read
                       if(!anyNews) { //add channel header if any news
                        string title = (feed("title"_)?feed("title"_):feed("channel"_)("title"_)).content;
                        string channel = (feed("link"_)?feed("link"_):feed("channel"_)("link"_)).content;
                        //TODO: favicons
                        news << Link(move(title), move(channel)); //TODO: bold
                        anyNews=true;
                      }
                       news << Link(copy(e("title"_).content), copy(e("link"_).content));
            });
        }
        window.show();
    }
    void keyPress(Key key) { if(key == Escape) quit(); }
} browser;
