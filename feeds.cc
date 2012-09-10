#include "feeds.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "interface.h"

ICON(network) ICON(feeds)

Feeds::Feeds() : readConfig("read"_,config(),File::ReadWrite|File::Create|File::Append), readMap(readConfig) {
    array::reserve(48);
    List<Entry>::activeChanged.connect(this,&Feeds::setRead);
    List<Entry>::itemPressed.connect(this,&Feeds::readEntry);
    load();
}

void Feeds::load() {
    clear(); favicons.clear();
    *this<<Entry(string(),string(":refresh"_),::resize(feedsIcon(),16,16),string("Feeds"_));
    for(TextData s=readFile("feeds"_,config());s;) { ref<byte> url=s.until('\n'); if(url[0]!='#') getURL(url, Handler(this, &Feeds::loadFeed), 60); }
}

bool Feeds::isRead(const ref<byte>& guid, const ref<byte>& link) {
    assert(!guid.contains('\n') && !link.contains('\n'));
    for(TextData s(readMap);s;s.until('\n')) {
        if(s.match(guid) && s.match(' ') && s.match(link)) return true;
    }
    return false;
}
bool Feeds::isRead(const Entry& entry) { return isRead(entry.guid, entry.link); }

void Feeds::loadFeed(const URL&, Map&& document) {
    Element feed = parseXML(document);
    Image* favicon=0;
    array<Entry> entries; int count=0;
    auto addEntry = [this,&favicon,&count,&entries](const Element& e)->void{
        if(count>=16) return; //limit history
        if(array::size()+entries.size()>=array::capacity()) return; //limit total entry count
        string title = e("title"_).text(); //RSS&Atom
        string guid = e("guid"_).text()?:e("pubDate"_).text(); //RSS
        string link = string(e("link"_)["href"_]); if(!link) link=e("link"_).text(); //Atom ?: RSS
        if(!favicon) {
            URL url = URL(link);
            favicon = &favicons[copy(url.host)];
            string faviconFile = cacheFile(url.relative("/favicon.ico"_));
            if(existsFile(faviconFile,cache())) *favicon = ::resize(decodeImage(readFile(faviconFile,cache())),16,16);
            else { *favicon = ::resize(networkIcon(),16,16); getURL(move(url), Handler(this, &Feeds::getFavicon), 7*24*60); }
        }
        if(!isRead(guid, link)) entries<< Entry(move(guid),move(link),share(*favicon),move(title)); //display all unread entries
        else if(count==0) entries<< Entry(move(guid),move(link),share(*favicon),move(title),12); //display last read entry
        count++;
    };
    feed.xpath("feed/entry"_,addEntry); //Atom
    feed.xpath("rss/channel/item"_,addEntry); //RSS
    for(int i=entries.size()-1;i>=0;i--) *this<< move(entries[i]); //oldest first
    listChanged();
}

void Feeds::getFavicon(const URL& url, Map&& document) {
    Element page = parseHTML(move(document));
    ref<byte> icon=""_;
    page.xpath("html/head/link"_, [&icon](const Element& e){ if(e["rel"_]=="shortcut icon"_||(!icon && e["rel"_]=="icon"_)) icon=e["href"_]; } );
    if(!icon) icon="/favicon.ico"_;
    if(url.relative(icon).path!=url.relative("/favicon.ico"_).path) symlink(string("../"_+cacheFile(url.relative(icon))), cacheFile(url.relative("/favicon.ico"_)),cache());
    heap<ImageLoader>(url.relative(icon), &favicons[copy(url.host)], function<void()>(this, &Feeds::resetFavicons), int2(16,16), 7*24*60*60);
}

void Feeds::resetFavicons() {
    for(Entry& entry: *this) { Image* favicon=favicons.find(URL(entry.link).host); if(favicon) entry.icon.image = share(*favicon); }
    listChanged();
}

void Feeds::setRead(uint index) {
    if(index==0) return; //:refresh
    Entry& entry = array::at(index);
    if(isRead(entry)) return;
    readConfig.write(string(entry.guid+" "_+entry.link+"\n"_));
    readMap = readConfig; //remap
    entry.text.setSize(12);
}

void Feeds::readEntry(uint index) {
    if(index==0) {load(); return; } //:refresh
    pageChanged( array::at(index).link, array::at(index).text.text, array::at(index).icon.image );
    if(index+1<count()-1) getURL(URL(array::at(index+1).link)); //preload next entry (TODO: preload image)
}

void Feeds::readNext() {
    if(index>0) setRead(index);
    for(uint i=index+1;i<count()-1;i++) { //next unread item
        if(!isRead(array::at(i))) {
            setActive(i);
            itemPressed(i);
            return;
        }
    }
    if(focus==this) focus=0; load(); pageChanged(""_,""_,Image()); //return to desktop
}
