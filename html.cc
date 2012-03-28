#include "html.h"
#include "array.cc"
#include "raster.h"

void HTML::load(array<byte>&& document) {
    expanding=true; clear();
    Element html = parseHTML(move(document));

    const Element* best=0; int max=0,second=0;
    //find node with most direct content
    html.visit([this,&best,&max,&second](const Element& div) {
        int score =0;
        if(div["class"_]=="content"_||div["id"_]=="content"_) score += 900;
        else if(contains(div["class"_],"content"_)||contains(div["id"_],"content"_)) score += 900;
        else if(startsWith(div["style"_],"background-image:url("_)) score += 16384;
        if(div.name=="img"_ && div["src"_]) {
            URL src = url.relative(div["src"_]);
            if(contains(src.path,"comic"_)||contains(src.path,"strip"_)||contains(src.path,"page"_)||contains(src.path,"chapter"_)||contains(src.path,"art/"_)) {
                int size=0;
                if(isInteger(div["width"_])&&isInteger(div["height"_])) size = toInteger(div["width"_])*toInteger(div["height"_]);
                score += size?:16300;
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
            } else if(e.name=="br"_) score += 32; //line break
        }
        if(score>max) best=&div, second=max, max=score;
        else if(score>second) second=score;
    });
    assert(best);
    while(best->name=="a"_ && best->children.size()==1) best=best->children.first();
    const Element& content = *best;

    //convert HTML to text + images
    layout(content);
    flushText();
    flushImages();

    /*log("TEXT\n",text);
    log("HTML\n"_+str(content));
    log("-------------------------------------------------\n",html);
    warn(url,max,second);*/
    contentChanged.emit();
}

void HTML::layout(const Element &e) { //TODO: keep same connection to load images
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
    } else if(e.name=="p"_||e.name=="br"_||e.name=="div"_) { //Paragraph
        flushImages();
        for(auto& c: e.children) layout(*c);
        text<<"\n"_;
    } else if(e.name=="strong"_||e.name=="em"_) { //Bold
        text << format(Bold);
        for(auto& c: e.children) layout(*c);
        text << format(Regular);
    } else { //Unknown
        for(auto& c: e.children) layout(*c);
        if(e.name=="a"_||e.name=="blockquote"_||e.name=="span"_||e.name=="li"_||e.name=="ul"_) {} //Ignored
        else log("Unknown element",e.name);
    }
}
void HTML::flushText() {
    string paragraph = simplify(trim(text));
    if(!paragraph) return;
    append( new Text(move(paragraph),16,255, 640 /*60 characters*/) );
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
        for(uint x=0;x<w && i<images.size();x++,i++) {
            *list << ImageView();
            getURL(images[i], &(*list).last(), &ImageView::load);
            (*list).last().imageChanged.connect(&this->contentChanged);
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
