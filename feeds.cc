#include "feeds.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "interface.h"
#include "array.cc"

ICON(network)

Feeds::Feeds() : config(openFolder("config"_)), readConfig(appendFile("read"_,config)), readMap(mapFile("read"_,config)) {
    List<Entry>::activeChanged.connect(this,&Feeds::setRead);
    List<Entry>::itemPressed.connect(this,&Feeds::readEntry);
    for(TextStream s(readFile("feeds"_,config));s;) getURL(s.until('\n'), Handler(this, &Feeds::loadFeed), 24*60);
}

bool Feeds::isRead(const ref<byte>& title, const ref<byte>& link) {
    assert(!contains(title,byte('\n')),title);
    assert(!contains(link,byte('\n')),link);
    for(TextStream s(readMap);s;s.until('\n')) { //feed+date (instead of title+link) would be less readable and fail on feed relocation
        if(s.match(title) && s.match(' ') && s.match(link)) return true;
    }
    return false;
}
bool Feeds::isRead(const Entry& entry) { return isRead(entry.get<Text>().text, entry.link); }

void Feeds::loadFeed(const URL&, array<byte>&& document) {
    Element feed = parseXML(document);
    string link = feed.text("rss/channel/link"_) ?: string(feed("feed"_)("link"_)["href"_]); //Atom
    string favicon = cacheFile(URL(link).relative("/favicon.ico"_));
    if(exists(favicon,cache)) {
        favicons.insert(link) = resize(decodeImage(readFile(favicon,cache)),16,16);
    } else {
        favicons.insert(link) = resize(networkIcon(),16,16);
        getURL(URL(link), Handler(this, &Feeds::getFavicon), 7*24*60);
    }
    array<Entry> entries; int history=0;
    auto addEntry = [this,&history,&entries](const Element& e)->void{
        if(history++>=32) return; //avoid getting old unreads on feeds with big history
        if(array::size()+entries.size()>=30) return;
        string title = e("title"_).text(); //RSS&Atom
        string url = unescape(e("link"_)["href"_]) ?: e("link"_).text(); //Atom ?: RSS
        assert(!contains(title,byte('\n')),e);
        if(!isRead(title, url)) entries<< Entry(move(title),move(url));
    };
    feed.xpath("feed/entry"_,addEntry); //Atom
    feed.xpath("rss/channel/item"_,addEntry); //RSS
    for(int i=entries.size()-1;i>=0;i--) *this<< move(entries[i]); //oldest first
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

void Feeds::setRead(uint index) {
    Entry& entry = array::at(index);
    if(isRead(entry)) return;
    ::write(readConfig,string(entry.get<Text>().text+" "_+entry.link+"\n"_));
    readMap = mapFile(readConfig); //remap
    entry.get<Text>().setSize(12);
}

void Feeds::readEntry(uint index) {
    pageChanged( array::at(index).link );
    if(index+1<count()) getURL(URL(array::at(index+1).link)); //preload next entry (TODO: preload images)
}

void Feeds::readNext() {
    setRead(index);
    for(uint i=index+1;i<count();i++) { //next unread item
        if(!isRead(array::at(i))) {
            setActive(i);
            itemPressed(i);
            return;
        }
    }
    //execute("/bin/sh"_,{"-c"_,"killall desktop; desktop&"_}); FIXME: implement free, fix memory leaks
    //clear(); for(TextStream s(readFile("feeds"_,config));s;) getURL(s.until('\n'), Handler(this, &Feeds::loadFeed), 24*60); //reload
    pageChanged(""_); //return to desktop
}
