#include "process.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "array.cc"
#include "interface.h"
#include "window.h"

struct Entry : Item {
    string link;
    Scroll<HTML>* content=0;
    Entry(Entry&& o) : Item(move(o)) { link=move(o.link); content=o.content; o.content=0; }
    Entry(string&& name, string&& link, Image&& icon=Image()):Item(move(icon),move(name)),link(move(link)){}
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

    Feeds(array<string>&& arguments) {
        news.reserve(256); //realloc would invalidate delegates
        window.localShortcut("Escape"_).connect(this, &Feeds::quit);
        news.activeChanged.connect(this,&Feeds::activeChanged);
        news.itemPressed.connect(this,&Feeds::itemPressed);
        if(arguments) {
            content = new Scroll<HTML>;
            content->contentChanged.connect(this, &Feeds::render);
            main << &content->parent();
            content->go(arguments.first());
        } else {
            array<string> feeds = split(readFile(".config/feeds"_,home()),'\n');
            for(const string& url: feeds) getURL(url, Handler(this, &Feeds::loadFeed), 15*60);
        }
        window.show();
    }
    ~Feeds() { close(readConfig); }

    bool isRead(const Entry& entry) {
        const Text& text = entry.get<Text>();
        string id = text.text+entry.link;
        return contains(read,id);
    }
    void setRead(const Entry& entry) {
        const Text& text = entry.get<Text>();
        string id = text.text+entry.link;
        id=replace(id,"\n"_,""_);
        if(contains(read,id)) return;
        read << id;
        ::write(readConfig,string(id+"\n"_));
    }
    void loadFeed(const URL&, array<byte>&& document) {
        Element feed = parseXML(move(document));

        //Header
        string title = feed.text("rss/channel/title"_); //RSS
        if(!title) title = feed("feed"_)("title"_).text(); //Atom
        assert(title,feed);
        array<string> words = split(title,' ');
        if(words.size()>4) title=join(slice(words,0,4)," "_);

        string link = feed.text("rss/channel/link"_); //RSS
        if(!link) link = feed("feed"_)("link"_)["href"_]; //Atom
        assert(link,feed);

        news << Entry(move(title),copy(link));

        string favicon = cacheFile(URL(link).relative("/favicon.ico"_));
        if(exists(favicon,cache)) {
            news.last().get<Icon>().image = resize(decodeImage(readFile(favicon,cache)),16,16);
        } else getURL(link, Handler(this, &Feeds::getFavicon), 7*24*60*60);

        int count=0;
        auto addItem = [this,&count](const Element& e)->void{
            if(count++>32) return;
            string text=e("title"_).text();
            text=trim(unescape(text));

            string url=e("link"_).text(); //RSS
            if(!url) url=e("link"_)["href"_]; //Atom

            Entry entry(move(text),move(url));
            if(!isRead(entry)) {
                if(news.count()>=256){warn("Too many news"); return;}
                news << move(entry);
                Entry& item = news.last();
                item.content= new Scroll<HTML>();
                item.content->go(item.link); //preload unread entries
            }
        };
        feed.xpath("feed/entry"_,addItem); //Atom
        feed.xpath("rss/channel/item"_,addItem); //RSS
        render();
    }
    void getFavicon(const URL& url, array<byte>&& document) {
        Element page = parseHTML(move(document));
        string icon;
        page.xpath("html/head/link"_,[&icon](const Element& e)->void{ if(e["rel"_]=="shortcut icon"_||(!icon && e["rel"_]=="icon"_)) icon=e["href"_]; } );
        if(!icon) icon="/favicon.ico"_;
        if(url.relative(icon).path!=url.relative("/favicon.ico"_).path) symlink("../"_+cacheFile(url.relative(icon)),cacheFile(url.relative("/favicon.ico"_)),cache);
        for(Entry& entry: news) {
            if(contains(entry.link,url.host)) {
                new ImageLoader(url.relative(icon), &entry.get<Icon>().image, delegate<void()>(this,&Feeds::render), int2(16,16), 7*24*60*60);
                break; //only header
            }
        }
    }

    void activeChanged(int index) {
        Entry& entry = news[index];
        if(!entry.content) return; //header
        if(!isRead(entry)) {
            Text& text = entry.get<Text>();
            setRead(entry);
            text.setSize(12);
        }
        //TODO: show if in cache
        render();
    }
    void itemPressed(int index) {
        Entry& entry = news[index];
        main.clear();
        if(content) content->contentChanged.disconnect(this);
        if(entry.content) {
            content= entry.content;
            content->contentChanged.connect(this, &Feeds::render);
            main << &news << &content->parent();
        } else {
            content= entry.content= new Scroll<HTML>();
            content->contentChanged.connect(this, &Feeds::render);
            main << &news << &content->parent();
            content->go(entry.link);
        }
        render();
    }
    void render() { if(window.visible) { main.update(); window.render(); }}
};
Application(Feeds)
