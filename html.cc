#include "html.h"
#include "png.h"
#include "jpeg.h"

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
                              " tbody tfoot thead acronym del video figure section source noembed caption tag"_);
    const Element* best = &html; int max=0,second=0;
    //find node with most direct content
    html.mayVisit([&url,&best,&max,&second](const Element& div)->bool{
        int score = 0;
        if(find(div["class"_],"comment"_)||find(div["class"_],"menu"_)) return false;
        if(find(div["id"_],"comment"_)||find(div["id"_],"menu"_)) return false;
        if(div["class"_]=="content"_||div["id"_]=="content"_) score += 600;
        else if(div["class"_]=="comic"_||div["id"_]=="comic"_) score += 140000;
        //else if(find(div["class"_],"content"_)||find(div["id"_],"content"_)) score += 600;
        else if(startsWith(div["style"_],"background-image:url("_)) score += 100000;
        if(div.name=="img"_ && div["src"_]) {
            URL src = url.relative(div["src"_]);
            if(!endsWith(src.path,".gif"_) && !startsWith(src.path,"ad/"_) && !find(src.path,"comment"_) &&
                    (find(src.path,"comic"_)||find(src.path,"strip"_)||find(div["alt"_],"Page"_)||find(div["title"_],"Page"_)||
                     find(src.path,"page"_)||find(src.path,"chapter"_)||find(src.path,"issue"_)||find(src.path,"art/"_))) {
                int size=0;
                if(isInteger(div["width"_])&&isInteger(div["height"_])) size = toInteger(div["width"_])*toInteger(div["height"_]);
                score += size?:140000;
            }
        } else if(!div.children) return false;
        div.mayVisit([&score](const Element& e)->bool{
            if(find(e["class"_],"comment"_)) return false;
            if(paragraphElement.contains(e.name)||textElement.contains(e.name)||boldElement.contains(e.name)) {
                return true; //visit children
            } else if(!e.name) {
                score += e.content.size(); //raw text
            } else if(e.name=="img"_||e.name=="iframe"_) {
                //int height = isInteger(e["height"_]) ? toInteger(e["height"_]) : 1;
                //if(e.name=="img"_ && !endsWith(e["src"_],".gif"_)) score += height; //image
            } /*else if(e.name=="br"_) { score += 40; //line break
            }*/ else if(ignoreElement.contains(e.name)) {
            } else if(!e.name.contains(':')) warn("load: Unknown HTML tag",e.name);
            return false;
        });
        if(score>=max) best=&div, second=max, max=score;
        else if(score>second) second=score;
        return true;
    });
    while(best->name=="a"_ && best->children.size()==1) best=&best->children.first();
    const Element& content = *best;
    //write(1,str(content));
    log(url,max,second);
    //convert HTML to text + images
    parse(url, content);
    flushText();
    flushImages();
    //log(count(),"elements");
    if(paragraphCount) contentChanged(); //else ImageLoader will signal
}

void HTML::parse(const URL& url, const Element &e) {
    if(find(e["class"_],"comment"_)) return;
    /***/ if(e.name=="img"_) { //Images
        if(endsWith(e["src"_],".gif"_)) return;
        flushText();
        images << url.relative(e["src"_]);
    }
    else if(e.name=="div"_ && startsWith(e["style"_],"background-image:url("_)) { //Images
        flushText();
        TextStream s(e["style"_]); s.match("background-image:url("_); ref<byte> src=s.until(')');
        images << url.relative(src);
    }
    else if(!e.name) { //Text
       flushImages();
       if(!e.name) text << e.content;
    }
    else if(e.name=="a"_) { //Link
        flushImages();
        bool inlineText=true; //TODO:
        e.visit([&inlineText](const Element& e){if(e.name&&!textElement.contains(e.name)) inlineText=false;});
        if(inlineText) text << format(Underline|Link) << e["href"_] << " "_;
        for(const Element& c: e.children) parse(url, c);
        if(inlineText) text << format(Regular);
    }
    else if(paragraphElement.contains(e.name)) { //Paragraph
        flushImages();
        for(const Element& c: e.children) parse(url, c);
        text<<'\n';
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
    else if(e.name=="span"_&&e["class"_]=="editsection"_) { return; }//wikipedia [edit]
    else if(textElement.contains(e.name)) { for(const Element& c: e.children) parse(url, c); } // Unhandled format tags
    else if(ignoreElement.contains(e.name)) { return; } // Ignored elements
    else if(!e.name.contains(':')) warn("Unknown element '"_+e.name+"'"_);
}
void HTML::flushText() {
    string paragraph = simplify(move(text));
    if(!paragraph) return; paragraphCount++;
    Text& textLayout = alloc<Text>(move(paragraph), 16, 255, 640 /*60 characters*/);
    textLayout.linkActivated.connect(this, &HTML::go);
    VBox::operator<<(&textLayout);
}
void HTML::flushImages() {
    if(!images) return;
    uint w=1,h=1; for(;;) {
        if(w*h>=images.size()) break; w++;
        if(w*h>=images.size()) break; h++;
    }
    for(uint y=0,i=0;y<h;y++) {
        HList<ImageView>& list = alloc< HList<ImageView> >();
        for(uint x=0;x<w && i<images.size();x++,i++) {
            list << ImageView();
            alloc<ImageLoader>(images[i], &list.last().image, contentChanged);
        }
        VBox::operator<<(&list);
    }
    images.clear();
}
