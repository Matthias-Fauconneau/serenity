#include "interface.h"
#include "window.h"
#include "font.h"
#include "raster.h"

#include "array.cc"
template class array<Widget*>;
template class array<Text>;
/// Sets the array size to \a size, filling with \a value
template<class T> void fill(array<T>& a, const T& value, int size) { a.reserve(size); a.setSize(size); for(int i=0;i<size;i++) new (&a[i]) T(copy(value)); }

/// ScrollArea

void ScrollArea::update() {
    int2 hint = widget().sizeHint();
    widget().position = min(int2(0,0), max(size-hint, widget().position));
    widget().size = max(hint, size);
    widget().update();
}

bool ScrollArea::mouseEvent(int2 position, Event event, Button button) {
    if(event==Press && (button==WheelDown || button==WheelUp) && size.y<widget().sizeHint().y) {
        int2& position = widget().position;
        int2 previous = position;
        position.y += button==WheelUp?-32:32;
        position = max(size-widget().sizeHint(),min(int2(0,0),position));
        if(position != previous) update();
        return true;
    }
    if(widget().mouseEvent(position-widget().position,event,button)) return true;
    return false;
}

void ScrollArea::ensureVisible(Widget& target) {
    widget().position = max(-target.position, min(size-(target.position+target.size), widget().position));
}

/// Layout

bool Layout::mouseEvent(int2 position, Event event, Button button) {
    for(int i=0;i<count();i++) { Widget& child=at(i);
        if(position >= child.position && position < child.position+child.size) {
            if(child.mouseEvent(position-child.position,event,button)) return true;
        }
    }
    return false;
}

void Layout::render(int2 parent) {
    push(Clip{parent+position,parent+position+size});
    for(int i=0;i<count();i++) { Widget& child=at(i);
        //if(position+child.position>=int2(-4,-4) && child.position+child.size<=size+int2(4,4))
        child.render(parent+position);
    }
    pop();
}

/// Widgets

int Widgets::count() const { return array::size(); }
Widget& Widgets::at(int i) { return *array::at(i); }

/// Linear

int2 Linear::sizeHint() {
    int width=0, expandingWidth=0;
    int height=0, expandingHeight=0;
    for(int i=0;i<count();i++) { Widget& child=at(i);
        int2 size=xy(child.sizeHint());
        if(size.y<0) expandingHeight=true;
        height = max(height,abs(size.y));
        if(size.x<0) expandingWidth=true;
        width += abs(size.x);
    }
    return xy(int2((this->expanding||expandingWidth)?-max(1,width):width,expandingHeight?-height:height));
}

void Linear::update() {
    if(!count()) return;
    int2 size = xy(this->size);
    int width = size.x /*remaining space*/, sharing=0 /*expanding count*/, expandingWidth=0, height=0;
    array<int> hints; fill(hints,-1,count()); array<int> sizes; fill(sizes,-1,count());
    //allocate fixed space and convert to expanding if not enough space
    for(int i=0;i<count();i++) {
        int2 sizeHint = xy(at(i).sizeHint());
        int hint = sizeHint.x; height=sizeHint.y<0?size.y:max(height,sizeHint.y);
        if(hint >= width) hint = -hint; //convert to expanding if not enough space
        if(hint >= 0) width -= (sizes[i]=hints[i]=hint); //allocate fixed size
        else {
            hints[i] = -hint;
            sharing++; //count expanding widgets
            expandingWidth += -hint; //compute minimum total expanding size
        }
    }
    assert(width>=0);
    width -= expandingWidth;
    bool expanding=sharing;
    if(!expanding) sharing=count(); //if no expanding: all widgets will share the extra space
    if(width > 0 || sharing==1) { //share extra space evenly between expanding/all widgets
        int extra = width/(sharing+!expanding); //if no expanding: keep space for margins
        for(int i=0;i<count();i++) {
            sizes[i] = hints[i] + ((!expanding || sizes[i]<0)?extra:0);
        }
        width -= extra*sharing; //remaining margin due to integer rounding
    } else { //reduce biggest widgets first until all fit
        for(int i=0;i<count();i++) if(sizes[i]<0) sizes[i]=hints[i]; //allocate all widgets
        while(width<-sharing) {
            int& first = max(sizes);
            int second=first; for(int e: sizes) if(e>second && e<first) second=e;
            int delta = first-second;
            if(delta==0 || delta>-width/sharing) delta=-width/sharing; //if no change or bigger than necessary
            assert(delta>0,width);
            first -= delta; width += delta;
        }
    }
    int2 pen = int2(width/2,(size.y-min(height,size.y))/2); //external margin
    for(int i=0;i<count();i++) {
        at(i).size = xy(int2(sizes[i],min(height,size.y)));
        at(i).position = xy(pen);
        at(i).update();
        pen.x += sizes[i];
    }
}

/// UniformGrid

int2 UniformGrid::sizeHint() {
    int2 max(0,0);
    for(int i=0;i<count();i++) {
        int2 size=at(i).sizeHint();
        max = ::max(max,size);
    }
    return int2(width,height)*max;
}

void UniformGrid::update() {
    int2 size(Widget::size.x/width,  Widget::size.y/height);
    int2 margin = (Widget::size - int2(width,height)*size) / 2;
    int i=0; for(int y=0;y<height;y++) for(int x=0;x<width;x++,i++) { if(i>=count()) return; Widget& child=at(i);
        child.position = margin + int2(x,y)*size; child.size=size;
    }
}

/// Text

// temporary structure to organize code
struct TextLayout {
    int size;
    int wrap;
    Font* font;
    FontMetrics metrics = font->metrics(size);
    vec2 pen = vec2(0,metrics.ascender);
    struct Character { int code; vec2 pos; const Glyph& glyph; };
    typedef array<Character> Word;
    array<Word> line;
    Word word;
    array<Character> text;

    void nextLine(bool justify) {
        if(!line) { pen.y+=metrics.height; return; }
        //justify
        float length=0; for(const Word& word: line) length+=word.last().pos.x+word.last().glyph.advance.x; //sum word length
        float space=0;
        if(justify) space = round((wrap-length)/line.size());
        if(space<=0||space>32) space = font->metrics(size,' ').advance.x; //compact

        //layout
        pen.x=0;
        for(const Word& word: line) {
            assert(word);
            for(Character c: word) {
                c.pos += pen;
                text << c;
            }
            pen.x += word.last().pos.x+word.last().glyph.advance.x+space;
        }
        line.clear();
        pen.x=0; pen.y+=metrics.height;
    }

    TextLayout(int size, int wrap, const string& source):size(size),wrap(wrap),font(&defaultSans) {
        uint previous=' ';
        Format format=Regular;
        for(uint c: source) {
            if(c==' '||c=='\t'||c=='\n') {//next word/line
                if(c==' ') previous = c;
                if(!word) { if(c=='\n') nextLine(false); continue; }
                float length=0; for(const Word& word: line) length+=word.last().pos.x+word.last().glyph.advance.x+font->glyph(size,' ').advance.x;
                if(wrap && length+word.last().pos.x+(c=='\n'?0:word.last().glyph.advance.x)>=wrap) nextLine(true);
                line << word;
                word.clear();
                pen.x=0;
                if(c=='\n') nextLine(false);
                continue;
            }
            if(c>=0x10&&c<0x20) { //10-1F format control (bitmask for bold,oblique,underline,strike)
                format = ::format(c);
                assert(format<4);
                Font* lookup[] = {&defaultSans,&defaultBold,&defaultItalic,&defaultBoldItalic};
                font=lookup[format];
                continue;
            }
            const Glyph& glyph = font->glyph(size,c);
            pen.x += font->kerning(previous,c);
            previous = c;
            assert(glyph.image||c==0xA0,hex(c));
            if(glyph.image) word << i(Character{ c, vec2(pen.x,0)+glyph.offset, glyph });
            pen.x += glyph.advance.x;
        }
        if(word) line<<word;
        if(line) nextLine(false);
    }
    int2 textSize() {
       int2 bb=zero;
       for(Character c: text) bb=max(bb,int2(c.pos)+c.glyph.image.size());
        bb.y -= metrics.descender;
        return int2(round(bb.x),round(bb.y));
    }
};

Text::Text(string&& text, int size, ubyte opacity, int wrap) : text(move(text)), size(size), opacity(opacity), wrap(wrap) {}
void Text::update(int wrap) {
    TextLayout layout(size, wrap>=0 ? wrap : Widget::size.x+wrap, text);
    blits.clear();
    for(const auto& c: layout.text) blits << i(Blit{int2(round(c.pos.x),round(c.pos.y)),c.glyph.image});
    textSize = layout.textSize();
}
int2 Text::sizeHint() {
    if(!textSize) update(0);
    assert(!text||(textSize.x>0&&textSize.y>0),textSize,"'"_+text+"'"_);
    return wrap?int2(-textSize.x,textSize.y):textSize;
}

void Text::render(int2 parent) {
    int2 offset = parent+position+max(int2(0,0),(Widget::size-textSize)/2);
    for(const Blit& b: blits) {
        //if(b.pos>=int2(-4,0) && b.pos+b.image.size()<=Widget::size+int2(2,2)) {
            blit(offset+b.pos, b.image, MultiplyAlpha, opacity);
        //}
    }
}

/// TextInput

bool TextInput::mouseEvent(int2 position, Event event, Button button) {
    if(event!=Press) return false;
    Window::focus=this;
    int x = position.x-(this->position.x+(Widget::size.x-textSize.x)/2);
    for(cursor=0;cursor<blits.size() && x>blits[cursor].pos.x+(int)blits[cursor].image.width/2;cursor++) {}
    if(button==MiddleButton) { string selection=Window::getSelection(); cursor+=selection.size(); text<<move(selection); update(); }
    return true;
}

bool TextInput::keyPress(Key key) {
    if(cursor>text.size()) cursor=text.size();
    /**/ if(key==Left && cursor>0) cursor--;
    else if(key==Right && cursor<text.size()) cursor++;
    else if(key==Delete && cursor<text.size()) { text.removeAt(cursor); update(); }
    else if(key==BackSpace && cursor>0) { text.removeAt(--cursor); update(); }
    else if(!(key&0xff00)) { text.insertAt(cursor++,(char)key); update(); }
    else return false;
    return true;
}

void TextInput::render(int2 parent) {
    Text::render(parent);
    if(Window::focus==this) {
        if(cursor>text.size()) cursor=text.size();
        int x = cursor < blits.size()? blits[cursor].pos.x : cursor>0 ? blits.last().pos.x+blits.last().image.width : 0;
        fill(parent+position+max(int2(0,0),(Widget::size-textSize)/2), int2(x,0), int2(x+1,Widget::size.y),gray(0));
    }
}

/// Slider

int2 Slider::sizeHint() { return int2(-height,height); }

void Slider::render(int2 parent) {
    if(maximum > minimum && value >= minimum && value <= maximum) {
        int x = size.x*(value-minimum)/(maximum-minimum);
        fill(parent+position, int2(0,0), int2(x,size.y), gray(128));
        fill(parent+position, int2(x,0), size, gray(192));
    } else {
        fill(parent+position, int2(0,0), size, gray(128));
    }
}

bool Slider::mouseEvent(int2 position, Event event, Button button) {
    if((event == Motion || event==Press) && button==LeftButton) {
        value = minimum+position.x*(maximum-minimum)/size.x;
        valueChanged.emit(value);
        return true;
    }
    return false;
}

/// Selection

bool Selection::mouseEvent(int2 position, Event event, Button button) {
    if(Layout::mouseEvent(position,event,button)) return true;
    if(event != Press) return false;
    if(button == WheelDown && index>0 && index<count()) { index--; activeChanged.emit(index); return true; }
    if(button == WheelUp && index>=0 && index<count()-1) { index++; activeChanged.emit(index); return true; }
    if(button != LeftButton) return false;
    for(int i=0;i<count();i++) { Widget& child=at(i);
        if(position>=child.position && position<child.position+child.size) {
            if(index!=i) { index=i; activeChanged.emit(index); }
            itemPressed.emit(index);
            return true;
        }
    }
    return false;
}

/// HighlightSelection

void HighlightSelection::render(int2 parent) {
    if(index>=0 && index<count()) {
        Widget& current = at(index);
        if(position+current.position>=int2(-4,-4) && current.position+current.size<=(size+int2(4,4))) {
            fill(parent+position, current.position, current.position+current.size, byte4(int4(255, 192, 128, 255)*224/255));
        }
    }
    Layout::render(parent);
}

/// TabSelection

void TabSelection::render(int2 parent) {
    Layout::render(parent);
    if(index<0 || index>=count()) {
        //darken whole tabbar
        fill(parent+position, int2(0,0), size, gray(224), Multiply);
        return;
    }
    Widget& current = at(index);

    //darken inactive tabs
    fill(parent+position, int2(0,0), int2(current.position.x, size.y), gray(224), Multiply);
    if(current.position.x+current.size.x<size.x-1-count()) //dont darken only margin
        fill(parent+position, int2(current.position.x+current.size.x, 0), size, gray(224), Multiply);
}

/// Icon
int2 ImageView::sizeHint() { return int2(image.width,image.height); }
void ImageView::render(int2 parent) {
    if(!image) return;
    blit(parent+position+(Widget::size-image.size())/2, image, Alpha);
}

/// TriggerButton

bool TriggerButton::mouseEvent(int2, Event event, Button button) {
    if(event==Press && button==LeftButton) { triggered.emit(); return true; }
    return false;
}

///  ToggleButton

ToggleButton::ToggleButton(const Image& enableIcon, const Image& disableIcon) : enableIcon(enableIcon), disableIcon(disableIcon) {}
int2 ToggleButton::sizeHint() { return int2(size,size); }
void ToggleButton::render(int2 parent) {
    if(!(enabled?disableIcon:enableIcon)) return;
    int size = min(Widget::size.x,Widget::size.y);
    blit(parent+position+(Widget::size-int2(size,size))/2, enabled?disableIcon:enableIcon, Alpha);
}
bool ToggleButton::mouseEvent(int2, Event event, Button button) {
    if(event==Press && button==LeftButton) { enabled = !enabled; toggled.emit(enabled); return true; }
    return false;
}
