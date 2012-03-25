#include "process.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "array.cc"
#include "interface.h"
#include "window.h"

struct Link : Item {
    string url;
    Link(string&& text, string&& url):Item(Image(),move(text)),url(move(url)){}
};

ICON(rss);

struct Browser : Application {
    List<Link> news;
    Scroll<HTML> page;
    HBox main { &news, &page.parent() };
    Window window{&main,"Feeds"_,move(rssIcon),int2(0,-1),224};
    Browser() {
        news.compact = true;
        window.bgOuter=window.bgCenter;
        window.keyPress.connect(this, &Browser::keyPress);
        news.activeChanged.connect(this,&Browser::activeChanged);
        news.itemPressed.connect(this,&Browser::itemPressed);
    }
    void keyPress(Key key) { if(key == Escape) quit(); }

    int readConfig = appendFile(".config/read"_,home());
    array<string> read = split(readFile(".config/read"_,home()),'\n');
    bool isRead(const Link& link) {
        const Text& text = link.get<Text>();
        string id = text.text+link.url;
        return contains(read,id);
    }
    void setRead(const Link& link) {
        const Text& text = link.get<Text>();
        string id = text.text+link.url;
        if(contains(read,id)) return;
        read << id;
        ::write(readConfig,string(id+"\n"_));
    }
    void start(array<string>&&) {
        array<string> feeds = split(readFile(".config/feeds"_,home()),'\n');

        for(const string& url: feeds) {
            auto document = getURL(url);
            if(!document) { log("Timeout: "_+url); continue; }
            Element feed = parseXML(move(document));

            //Header
            string title; feed.xpath("//title"_,[&title](const Element& e){title=e.text();});
            //string link; feed.xpath("//link"_,[&link](const Element& e){link=e.text();}); assert(link);
            assert(title);
            array<string> words = split(title,' ');
            if(words.size()>4) title=join(slice(words,0,4)," "_);
            //TODO: favicons
            array<Link> items;
            items << Link(move(title), ""_);

            auto addItem = [this,&feed,&items](const Element& e) {
                if(items.size()>16) return;

                string text=e("title"_).text();
                text=unescape(text);
                assert(text);

                string url=e("link"_).text(); //RSS
                if(!url) url=e("link"_)["href"_]; //Atom
                url=unescape(url);
                assert(url,e);

                Link link(move(text), move(url));
                link.get<Text>().setSize(isRead(link) ? 8 : 14);
                items << move(link);
            };
            feed.xpath("feed/entry"_,addItem); //Atom
            feed.xpath("rss/channel/item"_,addItem); //RSS
            //find oldest unread item
            uint i=items.size()-1; for(;i>0;i--) if(!isRead(items[i])) break;
            if(i==0) items.first().get<Text>().setSize(10); //Smaller header for fully read feed
            if(i+2<items.size()) items.shrink(i+2); //display only 2 older read item
            for(Link& item: items) news.append( move(item) );
        }
        window.show();
        Window::sync();
    }
    void activeChanged(int index) {
        Link& link = news[index];
        if(!link.url) return; //header
        if(!isRead(link)) {
            Text& text = link.get<Text>();
            setRead(link);
            text.setSize(8);
        }
        //TODO: show if in cache
#ifdef UI
        main.update(); window.render();
#endif
    }
    void itemPressed(int index) {
        Link& link = news[index];
        if(!link.url) return; //header

        page.clear();
        page << new Text("Loading"_+link.url+"..."_);
        main.update(); window.render();
        page.clear();
        page.load(news[index].url);

        main.update(); window.render();
    }
} browser;
