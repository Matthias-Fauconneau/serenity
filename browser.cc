#include "process.h"
#include "file.h"
#include "http.h"
#include "xml.h"
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
    Scroll<Text> page; //TODO: rich text
#define UI //can be disabled for debugging
#ifdef UI
    HBox main { &news, &page.parent() };
    Window window{&main,"Feeds"_,move(rssIcon),int2(0,-1),224};
    Browser() {
        window.bgOuter=window.bgCenter;
        page.wrap=true;
        window.keyPress.connect(this, &Browser::keyPress);
        news.activeChanged.connect(this,&Browser::activeChanged);
        news.itemPressed.connect(this,&Browser::itemPressed);
    }
    void keyPress(Key key) { if(key == Escape) quit(); }
#endif
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

        feeds=slice(feeds,0,6); /// TESTING

        for(const string& url: feeds) {
            auto document = getURL(url);
            if(!document) { log("timeout",url); continue; }
            Element feed = parseXML(move(document));
            bool anyNews=false; int count=0;
            auto addItem = [this,&anyNews,&feed,&count](const Element& e) {
                count++; if(count>16) return;

                if(!anyNews) { //add channel header if any news
                    string title; feed.xpath("//title"_,[&title](const Element& e){title=e.text();});
                    //string link; feed.xpath("//link"_,[&link](const Element& e){link=e.text();});
                    assert(title); //assert(link);
                    //TODO: favicons
                    news << Link(move(title), ""_); //TODO: bold
                    anyNews=true;
                }

                string text=e("title"_).text();
                text=unescape(text);
                assert(text);

                string url=e("link"_).text(); //RSS
                if(!url) url=e("link"_)["href"_]; //Atom
                url=unescape(url);
                assert(url,e);

                Link link(move(text), move(url));
                if(isRead(link)) link.get<Text>().setSize(8);
                else link.get<Text>().setSize(12);
                news << move(link);
            };
            feed.xpath("feed/entry"_,addItem); //Atom
            feed.xpath("rss/channel/item"_,addItem); //RSS
        }
#ifdef UI
        window.show();
        itemPressed(0);
        Window::sync();
#else
        for(const Link& link: news) {
          log("----"_+link.url+"----"_);
          string text = toText(link.url);
          log(text);
          //break;
        }
#endif
    }
    string toText(const URL& url) {
        auto document = getURL(url);
        if(!document) { log("Timeout"); return ""_; }
        Element html = parseHTML(move(document));

        const Element* best=0; int max=0,second=0;
        //find node with most direct content
        html.visit([&best,&max,&second,&url](const Element& div) {
            int score =0;
            if(div["class"_]=="content"_||div["id"_]=="content"_) score += 880;
            else if(contains(div["class"_],"content"_)||contains(div["id"_],"content"_)) score += 500;
            if(div.name=="img"_ && div["src"_]) {
                URL src = url.relative(div["src"_]);
                if(contains(src.path,"comic"_)||contains(src.path,"strip"_)||contains(src.path,"page"_)||contains(src.path,"chapter"_)) {
                    int size=0;
                    if(isInteger(div["width"_])&&isInteger(div["height"_])) size = toInteger(div["width"_])*toInteger(div["height"_]);
                    if(!size) score += 4096;
                }
            }
            else if(!div.children) return;
            array<const Element*> stack;
            for(auto& c: div.children) stack<<c;
            while(stack.size()) {
                const Element& e = *stack.pop();
                const array<string> contentElement = {"p"_,"a"_,"blockquote"_,"ul"_,"li"_,"em"_,"strong"_};
                if(contains(contentElement,e.name))
                    for(auto& c: e.children) stack<<c; //content
                else if(e.name!="script"_ && e.name!="style"_ && e.content)
                    score += e.content.size(); //raw text
                else if(e.name=="img"_||e.name=="iframe"_) {
                    //int width = isInteger(e["width"_]) ? toInteger(e["width"_]) : 1;
                    int height = isInteger(e["height"_]) ? toInteger(e["height"_]) : 1;
                    if(e.name=="img"_) score += height; //image
                    //else if(e.name=="iframe"_) score += width*height; //video
                } else if(e.name=="br"_)
                    score += 32; //line break
            }
            if(score>max) best=&div, second=max, max=score;
            else if(score>second) second=score;
        });
        assert(best);
        const Element& content = *best;
        //convert HTML to plain text
        string text;
        content.visit([&text](const Element& e) {
            if(e.name=="br"_) text<<"\n"_;
            else if(e.name=="img"_) text<<"["_+e["src"_]+"]"_;
            else if(e.name!="script"_&&e.name!="style"_) text<<e.content;
        });
        text = unescape(text);
        text = replace(text,"\r"_,""_);
        text = replace(text,"\n\n\n"_,"\n\n"_);

        if(max<600) {
            log("TEXT\n",text);
            log("HTML\n"_+str(content));
            log("-------------------------------------------------\n",html);
            error(max,second);
        }

        return text;
    }
    void activeChanged(int index) {
        Link& link = news[index];
        if(!link.url) return; //header
        Text& text = link.get<Text>();
        setRead(link);
        text.setSize(8);
        //TODO: show if in cache
#ifdef UI
        main.update(); window.render();
#endif
    }
    void itemPressed(int index) {
        Link& link = news[index];
        if(!link.url) return; //header
        string text = toText(news[index].url);
        page.setText(move(text));
        main.update(); window.render();
    }
} browser;
