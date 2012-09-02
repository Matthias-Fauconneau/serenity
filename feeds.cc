#include "feeds.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "interface.h"
#include "ico.h"

Feeds::Feeds() : config(openFolder(string(getenv("HOME"_)+"/.config"_),root(),true)), readConfig(appendFile("read"_,config)), readMap(mapFile(readConfig)) {
    array::reserve(48);
    List<Entry>::activeChanged.connect(this,&Feeds::setRead);
    List<Entry>::itemPressed.connect(this,&Feeds::readEntry);
    for(TextStream s=readFile("feeds"_,config);s;) { ref<byte> url=s.until('\n'); if(url[0]!='#') getURL(url, Handler(this, &Feeds::loadFeed), 12*60); }
}

bool Feeds::isRead(const ref<byte>& title, const ref<byte>& link) {
    assert(!title.contains('\n') && !link.contains('\n'));
    for(TextStream s(readMap);s;s.until('\n')) { //feed+date (instead of title+link) would be less readable and fail on feed relocation
        if(s.match(title) && s.match(' ') && s.match(link)) return true;
    }
    return false;
}
bool Feeds::isRead(const Entry& entry) { return isRead(entry.text.text, entry.link); }

ICON(network)
void Feeds::loadFeed(const URL&, Map&& document) {
    Element feed = parseXML(document);
    Image* favicon=0;
    static_array<Entry,16> entries; int count=0;
    auto addEntry = [this,&favicon,&count,&entries](const Element& e)->void{
        if(count>=16) return; //limit history
        if(array::size()+entries.size()>=array::capacity()) return; //limit total entry count
        string title = e("title"_).text(); //RSS&Atom
        string link = string(e("link"_)["href"_]); if(!link) link=e("link"_).text(); //Atom ?: RSS
        if(!favicon) {
            URL url = URL(link);
            favicon = &favicons[copy(url.host)];
            string faviconFile = cacheFile(url.relative("/favicon.ico"_));
            if(existsFile(faviconFile,cache)) *favicon = ::resize(decodeImage(readFile(faviconFile,cache)),16,16);
            else { *favicon = ::resize(networkIcon(),16,16); getURL(url, Handler(this, &Feeds::getFavicon), 7*24*60); }
        }
        if(!isRead(title, link)) entries<< Entry(move(link),share(*favicon),move(title)); //display all unread entries
        else if(count==0) entries<< Entry(move(link),share(*favicon),move(title),12); //display last read entry
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
    if(url.relative(icon).path!=url.relative("/favicon.ico"_).path) symlink(string("../"_+cacheFile(url.relative(icon))), cacheFile(url.relative("/favicon.ico"_)),cache);
    heap<ImageLoader>(url.relative(icon), &favicons[copy(url.host)], function<void()>(this, &Feeds::resetFavicons), int2(16,16), 7*24*60*60);
}

void Feeds::resetFavicons() {
    for(Entry& entry: *this) { Image* favicon=favicons.find(URL(entry.link).host); if(favicon) entry.icon.image = share(*favicon); }
    listChanged();
}

void Feeds::setRead(uint index) {
    Entry& entry = array::at(index);
    if(isRead(entry)) return;
    ::write(readConfig,string(entry.text.text+" "_+entry.link+"\n"_));
    readMap = mapFile(readConfig); //remap
    entry.text.setSize(12);
}

void Feeds::readEntry(uint index) {
    pageChanged( array::at(index).link, array::at(index).text.text, array::at(index).icon.image );
    if(index+1<count()) getURL(URL(array::at(index+1).link)); //preload next entry (TODO: preload image)
}

void Feeds::readNext() {
    if(index<count()) setRead(index);
    for(uint i=index+1;i<count();i++) { //next unread item
        if(!isRead(array::at(i))) {
            setActive(i);
            itemPressed(i);
            return;
        }
    }
    clear(); favicons.clear(); for(TextStream s=readFile("feeds"_,config);s;) { ref<byte> url=s.until('\n'); if(url[0]!='#') getURL(url, Handler(this, &Feeds::loadFeed), 12*60); } //reload
    pageChanged(""_,""_,Image()); //return to desktop
}
