#include "html.h"
#include "array.cc"

ImageLoader::ImageLoader(const URL& url, Image<byte4>* target, delegate<void()> imageLoaded, int2 size, uint maximumAge)
    : target(target), imageLoaded(imageLoaded), size(size) {
    getURL(url, Handler(this, &ImageLoader::load), maximumAge);
}

void ImageLoader::load(const URL&, array<byte>&& file) {
    Image<byte4> image = decodeImage(file);
    if(!image) return;
    if(size) *target = resize(image,size.x,size.y);
    else *target = move(image);
    imageLoaded();
    delete this;
}

static array<string> textElement, boldElement, ignoreElement;

void HTML::go(const ref<byte>& url) { this->url=url; getURL(url, Handler(this, &HTML::load), 24*60); }

void HTML::load(const URL& url, array<byte>&& document) { clear(); append(url,move(document)); }
void HTML::append(const URL& url, array<byte>&& document) {
    Element html = parseHTML(move(document));

    if(!textElement) {
        textElement = split("span p a blockquote center u hr ul li i strike cite em ol dt dl dd h1 h2 h3 h4 h5 code article small abbr aside th pre"_);
        boldElement = split("b strong h1 h2 h3 h4 h5 h6"_);
        ignoreElement = split("html body iframe noscript option select nav hgroup time fieldset footer base form script style title head meta link div"
                              " header label input textarea td tt font tr table left area map button sup param embed object noindex optgroup basefont"
                              " tbody tfoot thead acronym del video figure section source noembed caption"_);
    }
    const Element* best = &html; int max=0,second=0;
    //find node with most direct content
    html.visit([&url,&best,&max,&second](const Element& div){
        int score = 0;
        if(div["class"_]=="content"_||div["id"_]=="content"_) score += 900;
        else if(find(div["class"_],"content"_)||find(div["id"_],"content"_)) score += 400;
        else if(startsWith(div["style"_],"background-image:url("_)) score += 16384;
        if(div.name=="img"_ && div["src"_].size) {
            URL src = url.relative(div["src"_]);
            if(!endsWith(src.path,".gif"_)&&(find(src.path,"comics/"_)||find(src.path,"comic/"_)||find(src.path,"strip"_)||
                                             find(src.path,"page"_)||find(src.path,"chapter"_)||find(src.path,"issue"_)||find(src.path,"art/"_))) {
                int size=0;
                if(isInteger(div["width"_])&&isInteger(div["height"_])) size = toInteger(div["width"_])*toInteger(div["height"_]);
                score += size?:16800;
            }
        } else if(!div.children) return;
        div.mayVisit([&score](const Element& e)->bool{
            if(contains(textElement,e.name)||contains(boldElement,e.name)) {
                return true; //visit children
            } else if(!e.name) {
                score += e.content.size(); //raw text
            } else if(e.name=="img"_||e.name=="iframe"_) {
                int height = isInteger(e["height"_]) ? toInteger(e["height"_]) : 1;
                if(e.name=="img"_ && !endsWith(e["src"_],".gif"_)) score += height; //image
            } else if(e.name=="br"_) { score += 32; //line break
            } else if(contains(ignoreElement,e.name)) {
            } else if(!contains(e.name,(byte)':')) warn("load: Unknown HTML tag",e.name);
            return false;
        });
        if(score>=max) best=&div, second=max, max=score;
        else if(score>second) second=score;
    });
    while(best->name=="a"_ && best->children.size()==1) best=&best->children.first();
    const Element& content = *best;

    //convert HTML to text + images
    layout(url, content);
    flushText();
    flushImages();

    //log("HTML\n"_+str(content));
    warn(url,max,second,count());
    contentChanged.emit();
}

void HTML::layout(const URL& url, const Element &e) { //TODO: keep same connection to load images
    /***/ if(e.name=="img"_) { //Images
       flushText();
       images << url.relative(e["src"_]);
    }
    else if(e.name=="div"_ && startsWith(e["style"_],"background-image:url("_)) { //Images
        flushText();
        TextStream s = string(e["style"_]); s.match("background-image:url("_); ref<byte> src=s.until(')');
        images << url.relative(src);
    }
    else if(!e.name) { //Text
       flushImages();
       if(!e.name) text << e.content;
    }
    else if(e.name=="a"_) { //Link
        flushImages();
        bool inlineText=true; //TODO:
        e.visit([&inlineText](const Element& e){if(e.name&&!contains(textElement,e.name))inlineText=false;});
        if(inlineText) text << format(Format::Underline|Format::Link) << e["href"_] << " "_;
        for(const Element& c: e.children) layout(url, c);
        if(inlineText) text << format(Format::Regular);
    }
    else if(e.name=="p"_||e.name=="br"_||e.name=="div"_) { //Paragraph
        flushImages();
        for(const Element& c: e.children) layout(url, c);
        text<<"\n"_;
    }
    else if(contains(boldElement,e.name)) { //Bold
        text << format(Format::Bold);
        for(const Element& c: e.children) layout(url, c);
        text << format(Format::Regular);
    }
    else if(e.name=="em"_||e.name=="i"_) { //Italic
        text << format(Format::Italic);
        for(const Element& c: e.children) layout(url, c);
        text << format(Format::Regular);
    }
    else if(e.name=="span"_&&e["class"_]=="editsection"_) { return; }//wikipedia [edit]
    else if(contains(textElement,e.name)) { for(const Element& c: e.children) layout(url, c); } // Unhandled format tags
    else if(contains(ignoreElement,e.name)) { return; } // Ignored elements
    else if(!contains<byte>(e.name,':')) warn("layout: Unknown HTML tag",e.name);
}
void HTML::flushText() {
    string paragraph = simplify(trim(text));
    if(!paragraph) return;
    Text& textLayout = alloc<Text>(move(paragraph),16,255, 640 /*60 characters*/);
    textLayout.linkActivated = delegate<void(const ref<byte>&)>(this, &HTML::go);
    VBox::operator<<(&textLayout);
    text.clear();
}
void HTML::flushImages() {
    if(!images) return;
    uint w=1,h=1; for(;;) {
        if(w*h>=images.size()) break; w++;
        if(w*h>=images.size()) break; h++;
    }
    for(uint y=0,i=0;y<h;y++) {
        HList<ImageView>& list = alloc< HList<ImageView> >();
        list.reserve(w);
        for(uint x=0;x<w && i<images.size();x++,i++) {
            list << ImageView();
            alloc<ImageLoader>(images[i], &list.last().image, contentChanged);
        }
        VBox::operator<<(&list);
    }
    images.clear();
}

void HTML::clear() {
    for(Widget* w: *this) delete w;
    VBox::clear();
}
