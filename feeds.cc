#include "feeds.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "html.h"
#include "interface.h"

ICON(network) ICON(feeds)

Feeds::Feeds() : readConfig("read"_,config(),ReadWrite|Create|Append), readMap(readConfig) {
    array::reserve(48);
    List<Entry>::activeChanged.connect(this,&Feeds::setRead);
    List<Entry>::itemPressed.connect(this,&Feeds::readEntry);
    load();
}

void Feeds::load() {
    clear(); favicons.clear();
    *this<<Entry(string(),string(":refresh"_),::resize(feedsIcon(),16,16),string("Feeds"_));
    for(TextData s=readFile("feeds"_,config());s;) { ref<byte> url=s.line(); if(url[0]!='#') getURL(url, Handler(this, &Feeds::loadFeed), 60); }
}

bool Feeds::isRead(const ref<byte>& guid, const ref<byte>& link) {
    assert(!guid.contains('\n') && !link.contains('\n'));
    for(TextData s(readMap);s;s.line()) {
        if(s.match(guid) && s.match(' ') && s.match(link)) return true;
    }
    return false;
}
bool Feeds::isRead(const Entry& entry) { return isRead(entry.guid, entry.link); }

Favicon::Favicon(string&& host):host(move(host)){
    URL url(string("http://"_+this->host));
    if(existsFile(cacheFile(url.relative("/favicon.ico"_)),cache())) image=resize(decodeImage(readFile(cacheFile(url.relative("/favicon.ico"_)),cache())),16,16);
    else getURL(move(url), Handler(this, &Favicon::get), 7*24*60);
    if(!image) image = ::resize(networkIcon(),16,16);
}
void Favicon::get(const URL& url, Map&& document) {
    Element page = parseHTML(move(document));
    ref<byte> icon=""_;
    page.xpath("html/head/link"_, [&icon](const Element& e){ if(e["rel"_]=="shortcut icon"_||(!icon && e["rel"_]=="icon"_)) icon=e["href"_]; } );
    if(!icon) icon="/favicon.ico"_;
    if(url.relative(icon).path!=URL(host).relative("/favicon.ico"_).path) symlink(string("../"_+cacheFile(url.relative(icon))), cacheFile(URL(host).relative("/favicon.ico"_)),cache());
    heap<ImageLoader>(url.relative(icon), &image, function<void()>(this, &Favicon::update), int2(16,16), 7*24*60);
}
void Favicon::update() {
    for(Image* user: users) *user=share(image);
    imageChanged();
}

void Feeds::loadFeed(const URL& url, Map&& document) {
    Element feed = parseXML(document);
    string link = feed.text("rss/channel/link"_);
    if(!link) link=string(feed("feed"_)("link"_)["href"_]);
    assert(link,url);
    URL channel = URL(link); //RSS ?: Atom
    assert(channel.host);
    Favicon* favicon=0;
    for(Favicon* f: favicons) if(f->host==channel.host) { favicon=f; break; }
    if(!favicon) {
        assert(channel.host);
        favicon = &heap<Favicon>(move(channel.host));
        favicon->imageChanged.connect(&listChanged,&signal<>::operator());
        favicons << favicon;
    }

    array<Entry> entries; int count=0;
    auto addEntry = [this,&favicon,&count,&entries](const Element& e)->void{
        if(count>=16) return; //limit history
        if(array::size()+entries.size()>=array::capacity()) return; //limit total entry count
        string title = e("title"_).text(); //RSS&Atom
        string guid = e("guid"_).text(); if(!guid) guid=e("pubDate"_).text(); //RSS
        string link = string(e("link"_)["href"_]); if(!link) link=e("link"_).text(); //Atom ?: RSS
        if(!isRead(guid, link)) entries<< Entry(move(guid),move(link),share(favicon->image),move(title)); //display all unread entries
        else if(count==0) entries<< Entry(move(guid),move(link),share(favicon->image),move(title),12); //display last read entry
        count++;
    };
    feed.xpath("feed/entry"_,addEntry); //Atom
    feed.xpath("rss/channel/item"_,addEntry); //RSS
    for(int i=entries.size()-1;i>=0;i--) *this<< move(entries[i]); //oldest first
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
    pageChanged( array::at(index).link, toUTF8(array::at(index).text.text), array::at(index).icon.image );
    if(index+1<count()-1) getURL(URL(array::at(index+1).link)); //preload next entry (TODO: preload image)
}

void Feeds::readNext() {
    if(index>0) setRead(index);
    for(uint i: range(index+1,count())) { //next unread item
        if(!isRead(array::at(i))) {
            setActive(i);
            itemPressed(i);
            return;
        }
    }
    if(focus==this) focus=0; load(); pageChanged(""_,""_,Image()); //return to desktop
}
