#include "html.h"
#include "array.cc"
#include "raster.h"

ImageLoader::ImageLoader(const URL& url, Image* target, delegate<void> imageLoaded, int2 size, uint maximumAge)
    : target(target), imageLoaded(imageLoaded), size(size) {
    getURL(url, Handler(this, &ImageLoader::load), maximumAge);
}

void ImageLoader::load(const URL&, array<byte>&& file) {
    Image image = decodeImage(file);
    if(!image.size()) return;
    if(size) *target = resize(image,size.x,size.y);
    else *target = move(image);
    imageLoaded();
    delete this;
}

static const array<string> textElement = {"span"_,"p"_,"a"_,"blockquote"_,"center"_,"u"_,"hr"_,"ul"_,"li"_,"i"_
"cite"_,"b"_,"em"_,"strong"_,"ol"_,"dt"_,"dl"_,"h1"_,"h2"_,"h3"_,"h4"_,"h5"_,"code"_,"article"_,"small"_,"abbr"_,"aside"_};
static const array<string> ignoreElement = {"html"_,"body"_,"iframe"_,"noscript"_,"option"_,"select"_,"nav"_,"hgroup"_,"time"_,"footer"_,"base"_,
"form"_,"script"_,"title"_,"head"_,"meta"_,"link"_,"div"_,"header"_,"label"_,"input"_,"textarea"_,"td"_,"tr"_,"table"_,"left"_};

void HTML::go(const string& url) { getURL(url, Handler(this, &HTML::load), 30*60); }

void HTML::load(const URL& url, array<byte>&& document) {
    expanding=true; clear();
    Element html = parseHTML(move(document));

    const Element* best=0; int max=0,second=0;
    //find node with most direct content
    html.visit([&url,&best,&max,&second](const Element& div) {
        int score =0;
        if(div["class"_]=="content"_||div["id"_]=="content"_) score += 900;
        else if(contains(div["class"_],"content"_)||contains(div["id"_],"content"_)) score += 900;
        else if(startsWith(div["style"_],"background-image:url("_)) score += 16384;
        if(div.name=="img"_ && div["src"_]) {
            URL src = url.relative(div["src"_]);
            if(contains(src.path,"comic"_)||contains(src.path,"strip"_)||contains(src.path,"page"_)||
                    contains(src.path,"chapter"_)||contains(src.path,"issue"_)||contains(src.path,"art/"_)) {
                int size=0;
                if(isInteger(div["width"_])&&isInteger(div["height"_])) size = toInteger(div["width"_])*toInteger(div["height"_]);
                score += size?:16800;
            }
        }
        else if(!div.children) return;
        array<const Element*> stack;
        for(auto& c: div.children) stack<<c;
        while(stack.size()) {
            const Element& e = *stack.pop();
            if(contains(textElement,e.name)) {
                for(auto& c: e.children) stack<<c; //text
            } else if(!e.name) {
                score += e.content.size(); //raw text
            } else if(e.name=="img"_||e.name=="iframe"_) {
                //int width = isInteger(e["width"_]) ? toInteger(e["width"_]) : 1;
                int height = isInteger(e["height"_]) ? toInteger(e["height"_]) : 1;
                if(e.name=="img"_) score += height; //image
                //else if(e.name=="iframe"_) score += width*height; //video
            } else if(e.name=="br"_) { score += 32; //line break
            } else if(contains(ignoreElement,e.name)) {
            } else if(!contains(e.name,":"_)) warn("Unknown HTML tag",e.name);
        }
        if(score>=max) best=&div, second=max, max=score;
        else if(score>second) second=score;
    });
    assert(best);
    while(best->name=="a"_ && best->children.size()==1) best=best->children.first();
    const Element& content = *best;

    //convert HTML to text + images
    layout(url, content);
    flushText();
    flushImages();

    /*log("HTML\n"_+str(content));
    warn(url,max,second,count());*/
    contentChanged.emit();
}

void HTML::layout(const URL& url, const Element &e) { //TODO: keep same connection to load images
    if(e.name=="img"_) { //Images
       flushText();
       images << url.relative(e["src"_]);
    } else if(e.name=="div"_ && startsWith(e["style"_],"background-image:url("_)) { //Images
        flushText();
        TextBuffer s(copy(e["style"_])); s.match("background-image:url("_); string src=s.until(")"_);
        images << url.relative(src);
    } else if(!e.name) { //Text
       flushImages();
       if(!e.name) text << e.content;
    } else if(e.name=="a"_) { //Link
        flushImages();
        bool inlineText=true; //TODO:
        e.visit([&inlineText](const Element& e){if(e.name&&!contains(textElement,e.name))inlineText=false;});
        if(inlineText) text << format(Format::Underline|Format::Link) << e["href"_] << " "_;
        for(auto& c: e.children) layout(url, *c);
        if(inlineText) text << format(Format::Regular);
    } else if(e.name=="p"_||e.name=="br"_||e.name=="div"_) { //Paragraph
        flushImages();
        for(auto& c: e.children) layout(url, *c);
        text<<"\n"_;
    } else if(e.name=="strong"_||e.name=="em"_||e.name=="i"_) { //Emphasis
        text << format(Format::Italic);
        for(auto& c: e.children) layout(url, *c);
        text << format(Format::Regular);
    } else if(contains(textElement,e.name)) { for(auto& c: e.children) layout(url, *c); // Unhandled format tags
    } else if(contains(ignoreElement,e.name)) { return; // Ignored elements
    } else warn("Unknown HTML tag",e.name);
}
void HTML::flushText() {
    string paragraph = simplify(trim(text));
    if(!paragraph) return;
    auto textLayout = new Text(move(paragraph),16,255, 640 /*60 characters*/);
    textLayout->linkActivated.connect(this, &HTML::go);
    append(textLayout);
    text.clear();
}
void HTML::flushImages() {
    if(!images) return;
    uint w=1,h=1; for(;;) {
        if(w*h>=images.size()) break; w++;
        if(w*h>=images.size()) break; h++;
    }
    for(uint y=0,i=0;y<h;y++) {
        auto list = new HList<ImageView>;
        list->reserve(w);
        for(uint x=0;x<w && i<images.size();x++,i++) {
            *list << ImageView();
            new ImageLoader(images[i], &(*list).last().image,delegate<void>(&this->contentChanged,&signal<>::emit));
        }
        append( list );
    }
    images.clear();
}

void HTML::clear() {
    for(Widget* w: *this) delete w;
    VBox::clear();
}

void HTML::render(int2 parent) {
    fill(parent+position+Rect(int2(0,0),Widget::size),byte4(240,240,240,240));
    VBox::render(parent);
}
