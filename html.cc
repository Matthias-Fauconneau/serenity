#include "html.h"
#include "png.h"
#include "jpeg.h"
#include "ico.h"

ImageLoader::ImageLoader(const URL& url, Image* target, function<void()>&& imageLoaded, int2 size, uint maximumAge)
    : target(target), imageLoaded(imageLoaded), size(size) {
    getURL(url, Handler(this, &ImageLoader::load), maximumAge);
}

void ImageLoader::load(const URL&, Map&& file) {
    Image image = decodeImage(file);
    if(!image) return;
    if(size) image = resize(image,size.x,size.y);
    *target = move(image);
    imageLoaded();
    free(this);
}

static array< ref<byte> > paragraphElement, textElement, boldElement, ignoreElement;

void HTML::go(const ref<byte>& url) { this->url=url; getURL(url, Handler(this, &HTML::load), 24*60); }
void HTML::load(const URL& url, Map&& document) {
    for(Widget* w: *this) free(w); VBox::clear(); paragraphCount=0;

    Element html = parseHTML(document);

    if(!paragraphElement)
        paragraphElement = split("p br div"_);
    if(!textElement)
        textElement = split("span a blockquote center u hr ul li i strike cite em ol dt dl dd h1 h2 h3 h4 h5 code article small abbr aside th pre"_);
    if(!boldElement)
        boldElement = split("b strong h1 h2 h3 h4 h5 h6"_);
    if(!ignoreElement)
        ignoreElement = split("html body iframe noscript option select nav hgroup time fieldset footer base form script style title head meta link"
                              " header label input textarea td tt font tr table left area map button sup param embed object noindex optgroup basefont"
                              " tbody tfoot thead acronym del video figure section source noembed caption tag figcaption"_);
    const Element* best = &html; int max=0,second=0;
    //find node with most direct content
    html.mayVisit([&url,&best,&max,&second](const Element& e)->bool{
        int score = 0;
        if(find(e["class"_],"comment"_)||find(e["class"_],"menu"_)) return true;
        if(find(e["id"_],"comment"_)||find(e["id"_],"menu"_)) return true;
        if(e["class"_]=="content"_||e["id"_]=="content"_) score += 600;
        else if(e["class"_]=="comic"_||e["id"_]=="comic"_) score += 9000;
        else if(startsWith(e["style"_],"background-image:url("_)) score += 4000;
        if(e.name=="img"_ && e["src"_]) {
            URL src = url.relative(e["src"_]);
            if(!endsWith(src.path,".gif"_) && !startsWith(src.path,"ad/"_) && !find(src.path,"comment"_) &&
                    (find(src.path,"comic"_)||find(src.path,"strip"_)||find(e["alt"_],"Page"_)||find(e["title"_],"Page"_)||
                     find(src.path,"page"_)||find(src.path,"chapter"_)||find(src.path,"issue"_)||find(src.path,"art/"_))) {
                int size=0;
                if(isInteger(e["width"_])&&isInteger(e["height"_])) size = toInteger(e["width"_])*toInteger(e["height"_]);
                score += size?: find(e["alt"_],"Comic"_)? 4000: 0;
            }
        } else if(!e.children) return false;
        e.mayVisit([&score](const Element& e)->bool{
                if(!e.name) score += trim(e.content).size; //raw text
                else if(find(e["class"_],"comment"_)) return false;
                else if(paragraphElement.contains(e.name)||textElement.contains(e.name)||boldElement.contains(e.name)) return true; //visit children
                else if(e.name=="img"_||ignoreElement.contains(e.name)) {}
                else if(!e.name.contains(':')) warn("load: Unknown HTML tag",e.name);
                return false;
        });
        if(score>=max) best=&e, second=max, max=score;
        else if(score>second) second=score;
        return true;
    });
    while(best->name=="a"_ && best->children.size()==1) best=&best->children.first();
    const Element& content = *best;
    log(url,max,second);
    parse(url, content);
    flushText();
    flushImages();
    if(paragraphCount || !count()) contentChanged(); //else ImageLoader will signal
}

void HTML::parse(const URL& url, const Element &e) {
    if(!e.name) {
        if(text || trim(e.content)) { flushImages(); text << e.content; }
    }
    else if(find(e["class"_],"comment"_) || e["class"_]=="editsection"_) return;
    else if(e.name=="img"_) {
        if(endsWith(e["src"_],".gif"_)) return;
        flushText();
        images << url.relative(e["src"_]);
    }
    else if(e.name=="div"_ && startsWith(e["style"_],"background-image:url("_)) {
        TextStream s(e["style"_]); s.match("background-image:url("_); ref<byte> src=s.until(')');
        flushText();
        images << url.relative(src);
    }
    else if(e.name=="a"_) { //Link
        bool inlineText=true;
        e.visit([&inlineText](const Element& e){if(e.name&&!textElement.contains(e.name)) inlineText=false;});
        if(inlineText) flushImages();
        if(inlineText && !e["href"_].contains(' ')) text << format(Underline|Link) << e["href"_] << " "_;
        for(const Element& c: e.children) parse(url, c);
        if(inlineText && !e["href"_].contains(' ')) text << format(Regular);
    }
    else if(paragraphElement.contains(e.name)) { //Paragraph
        for(const Element& c: e.children) parse(url, c);
        if(text) { flushImages(); text<<'\n'; }
    }
    else if(boldElement.contains(e.name)) { //Bold
        text << format(Bold);
        for(const Element& c: e.children) parse(url, c);
        text << format(Regular);
        if(startsWith(e.name,"h"_)) text<<'\n';
    }
    else if(e.name=="em"_||e.name=="i"_) { //Italic
        text << format(Italic);
        for(const Element& c: e.children) parse(url, c);
        text << format(Regular);
    }
    else if(textElement.contains(e.name)) { for(const Element& c: e.children) parse(url, c); } // Unhandled format tags
    else if(ignoreElement.contains(e.name)) { return; } // Ignored elements
    else if(!e.name.contains(':')) warn("Unknown element '"_+e.name+"'"_);
}
void HTML::flushText() {
    string paragraph = simplify(move(text));
    if(!paragraph) return; paragraphCount++;
    Text& textLayout = heap<Text>(move(paragraph), 16, 255, 640 /*60 characters*/);
    textLayout.linkActivated.connect(this, &HTML::go);
    VBox::operator<<(&textLayout);
}
void HTML::flushImages() {
    if(!images) return;
    UniformGrid<ImageView>& grid = heap<UniformGrid<ImageView> >();
    grid.resize(images.size());
    for(URL& image: images) heap<ImageLoader>(image, &grid.last().image, contentChanged);
    VBox::operator<<(&grid);
    images.clear();
}
