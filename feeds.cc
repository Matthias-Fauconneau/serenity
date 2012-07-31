#include "feeds.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "interface.h"
#include "array.cc"

Feeds::Feeds() {
    static int config = openFolder("config"_);
    readConfig = appendFile("read"_,config);
    readMap = mapFile("read"_,config);
    List<Entry>::activeChanged.connect(this,&Feeds::activeChanged);
    List<Entry>::itemPressed.connect(this,&Feeds::itemPressed);
    for(TextStream s(readFile("feeds"_,config));s;) getURL(s.until('\n'), Handler(this, &Feeds::loadFeed), 24*60);
}
Feeds::~Feeds() { closeFile(readConfig); }

bool Feeds::isRead(const Entry& entry) {
    const Text& text = entry.get<Text>();
    string id = text.text+entry.link;
    id=replace(id,"\n"_,""_);
    for(TextStream s(readMap);s;s.until('\n')) if(s.match(id)) return true; return false;
}
void Feeds::setRead(const Entry& entry) {
    const Text& text = entry.get<Text>();
    string id = text.text+entry.link;
    id=replace(id,"\n"_,""_);
    for(TextStream s(readMap);s;s.until('\n')) if(s.match(id)) return;
    ::write(readConfig,string(id+"\n"_));
    readMap = mapFile(readConfig); //remap
}
void Feeds::setAllRead() {
    for(Entry& entry: *this) {
        setRead(entry);
    }
}

void Feeds::loadFeed(const URL&, array<byte>&& document) {
    Element feed = parseXML(move(document));
#if HEADER
    //Header
    string title = feed.text("rss/channel/title"_); //RSS
    if(!title) title = feed("feed"_)("title"_).text(); //Atom
    if(!title) { warn("Invalid feed"_,url); return; }
    //title.shrink(section(title,' ',0,4).size);

    string link = feed.text("rss/channel/link"_); //RSS
    if(!link) link = string(feed("feed"_)("link"_)["href"_]); //Atom
    if(!link) { warn("Invalid feed"_,url); return; }

    Entry header(move(title),copy(link));
    header.isHeader=true;
    append( move(header) );

    string favicon = cacheFile(URL(link).relative("/favicon.ico"_));
    if(exists(favicon,cache)) {
        last().get<Icon>().image = resize(decodeImage(readFile(favicon,cache)),16,16);
    } else {
        last().get<Icon>().image = resize(networkIcon,16,16);
        getURL(link, Handler(this, &Feeds::getFavicon), 7*24*60);
    }
#endif
    array<Entry> items; int history=0;
    auto addItem = [this,&history,&items](const Element& e)->void{
        if(history++>=64) return; //avoid getting old unreads on feeds with big history
        if(array::size()>=32) return;
        string text = e("title"_).text();
        text = string(trim(unescape(text)));

        string url = e("link"_).text(); //RSS
        if(!url) url = string(e("link"_)["href"_]); //Atom

        Entry entry(move(text),move(url));
        if(!isRead(entry)) items<< move(entry);
    };
    feed.xpath("feed/entry"_,addItem); //Atom
    feed.xpath("rss/channel/item"_,addItem); //RSS
    for(int i=items.size()-1;i>=0;i--) { //oldest first
        *this<< move(items[i]);
        //getURL(URL(last().link)); //preload
    }
    listChanged();
}

void Feeds::getFavicon(const URL& url, array<byte>&& document) {
    Element page = parseHTML(move(document));
    ref<byte> icon=""_;
    page.xpath("html/head/link"_,
               [&icon](const Element& e){ if(e["rel"_]=="shortcut icon"_||(!icon && e["rel"_]=="icon"_)) icon=e["href"_]; } );
    if(!icon) icon="/favicon.ico"_;
    if(url.relative(icon).path!=url.relative("/favicon.ico"_).path) {
        symlink(string("../"_+cacheFile(url.relative(icon))), cacheFile(url.relative("/favicon.ico"_)),cache);
    }
    for(Entry& entry: *this) {
        if(find(entry.link,url.host)) {
            alloc<ImageLoader>(url.relative(icon), &entry.get<Icon>().image, &listChanged, int2(16,16), 7*24*60*60);
            break; //only header
        }
    }
}

void Feeds::activeChanged(int index) {
    Entry& entry = array::at(index);
    if(entry.isHeader) return;
    if(!isRead(entry)) {
        Text& text = entry.get<Text>();
        //setRead(entry);
        text.setSize(12);
    }
}

void Feeds::itemPressed(int index) { pageChanged( array::at(index).link ); }

void Feeds::readNext() {
    uint i=index;
    for(;;) { //next unread item
        i++;
        if(i==count()) { /*execute("/bin/sh"_,{"-c"_,"killall desktop; desktop&"_}); FIXME: fix memory leaks*/ return; }
        if(!array::at(i).isHeader && !isRead(array::at(i))) break;
    }
    setActive(i);
    itemPressed(i);
}
