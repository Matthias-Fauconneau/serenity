#include "process.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "array.cc"
#include "interface.h"
#include "window.h"

struct Entry : Item {
    Scroll<HTML>* content; //allocate on heap to avoid dangling HTML::load delegates
    Entry(Entry&& o) : Item(move(o)) { content=o.content; o.content=0; }
    Entry(const string& text, const string& url):Item(Image(),copy(text)),content(new Scroll<HTML>()){ if(url) content->url=url; }
    ~Entry() { if(content) delete content; }
};

ICON(feeds);

struct Feeds : Application {
    List<Entry> news;
    HBox main { &news };
    Window window{&main,"Feeds"_,move(feedsIcon),int2(0,0),224};
    Scroll<HTML>* content=0;

    int readConfig = appendFile(".config/read"_,home());
    array<string> read = split(readFile(".config/read"_,home()),'\n');

    Feeds(array<string>&& /*arguments*/) {
        window.bgOuter=window.bgCenter;
        window.localShortcut("Escape"_).connect(this, &Feeds::quit);
        news.activeChanged.connect(this,&Feeds::activeChanged);
        news.itemPressed.connect(this,&Feeds::itemPressed);
        window.show();
        /*if(arguments) {
            page.load(arguments.first());
        } else {*/
            array<string> feeds = split(readFile(".config/feeds"_,home()),'\n');
            for(const string& url: feeds) getURL(url, this, &Feeds::loadFeed);
        //}
        Window::sync();
    }
    ~Feeds() { close(readConfig); }

    bool isRead(const Entry& entry) {
        const Text& text = entry.get<Text>();
        string id = text.text+str(entry.content->url);
        return contains(read,id);
    }
    void setRead(const Entry& entry) {
        const Text& text = entry.get<Text>();
        string id = text.text+str(entry.content->url);
        id=replace(id,"\n"_,""_);
        if(contains(read,id)) return;
        read << id;
        ::write(readConfig,string(id+"\n"_));
    }
    void loadFeed(array<byte>&& document) {
        Element feed = parseXML(move(document));

        //Header
        string title; feed.xpath("//title"_,[&title](const Element& e){title=e.text();});
        assert(title);
        array<string> words = split(title,' ');
        if(words.size()>4) title=join(slice(words,0,4)," "_);
        //TODO: favicons
        array<Entry> items;
        items << Entry(move(title), ""_);

        auto addItem = [this,&feed,&items](const Element& e) {
            if(items.size()>16) return;

            string text=e("title"_).text();
            text=trim(unescape(text));

            string url=e("link"_).text(); //RSS
            if(!url) url=e("link"_)["href"_]; //Atom

            Entry entry(move(text), move(url));
            entry.get<Text>().setSize(isRead(entry) ? 8 : 14);
            items << move(entry);
        };
        feed.xpath("feed/entry"_,addItem); //Atom
        feed.xpath("rss/channel/item"_,addItem); //RSS
        //find oldest unread item
        uint i=items.size()-1; for(;i>0;i--) if(!isRead(items[i])) break;
        if(i==0) items.first().get<Text>().setSize(10); //Smaller header for fully read feed
        if(i+2<items.size()) items.shrink(i+2); //display only 2 older read item
        for(Entry& item: items) {
            if(item.content->url && !isRead(item)) getURL(item.content->url, item.content, &HTML::load); //preload unread entries
            news.append( move(item) );
        }
        update();
    }
    void activeChanged(int index) {
        Entry& entry = news[index];
        if(!entry.content->url) return; //header
        if(!isRead(entry)) {
            Text& text = entry.get<Text>();
            setRead(entry);
            text.setSize(8);
        }
        //TODO: show if in cache
        main.update(); window.render();
    }
    void itemPressed(int index) {
        Entry& entry = news[index];
        if(!entry.content->url) return; //header
        main.clear();
        if(content) content->contentChanged.disconnect(this);
        content=entry.content;
        assert(content);
        content->contentChanged.connect(this,&Feeds::update);
        main << &news << &content->parent();
        if(!content->count()) getURL(content->url, content, &HTML::load);
        else update();
    }
    void update() { main.update(); window.render(); Window::sync(); }
};
Application(Feeds)
