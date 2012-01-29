#include "interface.h"
#include "window.h"
#include "font.h"
#include "gl.h"

/// ScrollArea

void ScrollArea::update() {
    int2 hint = widget().sizeHint();
    widget().position = min(int2(0,0), max(size-hint, widget().position));
    widget().size = max(hint, size);
    widget().update();
}

bool ScrollArea::mouseEvent(int2 position, Event event, Button button) {
    if(widget().mouseEvent(position-widget().position,event,button)) return true;
    if(event==Press && (button==WheelDown || button==WheelUp)) {
        if(size>=widget().sizeHint()) return false;
        int2& position = widget().position;
        int2 previous = position;
        position.y += button==WheelUp?-32:32;
        position = max(size-widget().sizeHint(),min(int2(0,0),position));
        if(position != previous) update();
        return true;
    }
    return false;
}

void ScrollArea::ensureVisible(Widget& target) {
    widget().position = max(-target.position, min(size-(target.position+target.size), widget().position));
}

/// Layout

bool Layout::mouseEvent(int2 position, Event event, Button button) {
    for(auto& child : *this) {
        if(position >= child.position && position < child.position+child.size) {
            if(child.mouseEvent(position-child.position,event,button)) return true;
        }
    }
    return false;
}

void Layout::render(int2 parent) {
    for(auto& child : *this) {
        if(position+child.position>=int2(-4,-4) && child.position+child.size<=size+int2(4,4))
            child.render(parent+position);
    }
}

/// Linear

int2 Linear::sizeHint() {
	int width=0, expanding=0, height=0;
	for(auto& child : *this) {
        int2 size=xy(child.sizeHint());
        if(size.y<0) height = min(height,size.y);
        if(height>=0) height = max(height,size.y);
        if(size.x<0) expanding++;
        width += abs(size.x);
	}
    return xy(int2((this->expanding||expanding)?-width:width,height));
}

void Linear::update() {
    if(!count()) return;
    int2 size = xy(this->size);
    int width = size.x /*remaining space*/, sharing=0 /*expanding count*/, expandingWidth=0;
    array<int> hints(count(),-1); array<int> sizes(count(),-1);
    //allocate fixed space and convert to expanding if not enough space
    for(int i=0;i<count();i++) {
        int hint = xy(at(i).sizeHint()).x;
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
    int2 pen = int2(width/2,0); //external margin
    for(int i=0;i<count();i++) {
        at(i).size = xy(int2(sizes[i],size.y));
        at(i).position = xy(pen);
        at(i).update();
        pen.x += sizes[i];
    }
}

/// Text

Text::Text(string&& text, int size) : text(move(text)), size(size), font(defaultFont) { update(); }
void Text::update() {
	int widestLine = 0;
    FontMetrics metrics = font.metrics(size);
	vec2 pen = vec2(0,metrics.ascender);
	blits.clear();
    int previous=0;
    for(int c: text) {
		if(c=='\n') { widestLine=max(int(pen.x),widestLine); pen.x=0; pen.y+=metrics.height; continue; }
        const Glyph& glyph = font.glyph(size,c);
		if(previous) pen.x += font.kerning(previous,c);
		previous = c;
		if(glyph.texture) {
			Blit blit = { pen+glyph.offset, pen+glyph.offset+vec2(glyph.texture.width,glyph.texture.height), glyph.texture.id };
			blits << blit;
		}
		pen.x += glyph.advance.x;
	}
	widestLine=max(int(pen.x),widestLine);
	textSize=int2(widestLine,pen.y-metrics.descender);
}
int2 Text::sizeHint() { if(!textSize) update(); assert(textSize); return textSize; }

void Text::render(int2 parent) {
    blit.bind(); blit["offset"]=vec2(parent+position+max(int2(0,0),(Widget::size-textSize)/2));
    for(const auto& b: blits) {
        if(b.min>=vec2(-4,0) && b.max<=vec2(Widget::size+int2(2,2))) {
            GLTexture::bind(b.id); glQuad(blit, b.min, b.max, true);
        }
    }
}

/// TextInput

void TextInput::update() {
    Text::update();
    cursor=clip(0,cursor,text.size);
}

bool TextInput::mouseEvent(int2 position, Event event, Button button) {
    if(event!=Press || button!=LeftButton) return false;
    Window::focus=this;
    int x = position.x-(this->position.x+(Widget::size.x-textSize.x)/2);
    for(cursor=0;cursor<blits.size && x>(blits[cursor].min.x+blits[cursor].max.x)/2;cursor++) {}
    return true;
}

bool TextInput::keyPress(Key key) {
    cursor=clip(0,cursor,text.size);
    /**/ if(key==Left && cursor>0) cursor--;
    else if(key==Right && cursor<text.size) cursor++;
    else if(key==Delete && cursor<text.size) { text.removeAt(cursor); update(); }
    else if(key==BackSpace && cursor>0) { text.removeAt(--cursor); update(); }
    else if(!(key&0xff00)) { text.insertAt(cursor++,(char)key); update(); }
    else return false;
    return true;
}

void TextInput::render(int2 parent) {
    Text::render(parent);
    if(Window::focus==this) {
        flat.bind(); flat["offset"]=vec2(parent+position+(Widget::size-textSize)/2); flat["color"]=vec4(0,0,0,1);
        int x = cursor < blits.size? blits[cursor].min.x : cursor>0 ? blits.last().max.x : 0;
        glQuad(flat,vec2(x,0),vec2(x+1,Widget::size.y));
    }
}

/// Slider

int2 Slider::sizeHint() { return int2(-height,height); }

void Slider::render(int2 parent) {
    flat.bind(); flat["offset"]=vec2(parent+position); flat["color"]=vec4(1./2, 1./2, 1./2, 1);
    if(maximum > minimum && value >= minimum && value <= maximum) {
		int x = size.x*(value-minimum)/(maximum-minimum);
		glQuad(flat,vec2(0,0),vec2(x,size.y));
        flat["color"]=vec4(3./4, 3./4, 3./4, 1);
		glQuad(flat,vec2(x,0),vec2(size));
	} else {
		glQuad(flat,vec2(0,0),vec2(size));
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
    if(event != Press || button != LeftButton) return false;
    int i=0;
    for(auto& child: *this) {
        if(position>child.position && position<child.position+child.size) {
            index=i; activeChanged.emit(i);
			return true;
		}
		i++;
	}
	return false;
}

void Selection::render(int2 parent) {
    Layout::render(parent);
    if(index<0 || index>=count()) return;
    Widget& current = at(index);
    if(position+current.position>=int2(-4,-4) && current.position+current.size<=(size+int2(4,4))) {
        flat.bind(); flat["offset"]=vec2(parent+position); flat["color"]=vec4(3./4, 7./8, 1, 1); //multiply blend
        glQuad(flat,vec2(current.position), vec2(current.position+current.size));
    }
}

/// Icon

Icon::Icon(const Image& image) : image(image) {}
int2 Icon::sizeHint() { return int2(image.width,image.height); }
void Icon::render(int2 parent) {
    if(!image) return;
    int size = min(Widget::size.x,Widget::size.y);
    int2 offset = (Widget::size-int2(size,size))/2;
    blit.bind(); blit["offset"]=vec2(parent+position+offset);
    image.bind();
    glQuad(blit,vec2(0,0),vec2(size,size),true);
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
    int2 offset = (Widget::size-int2(size,size))/2;
    blit.bind(); blit["offset"]=vec2(parent+position+offset);
	(enabled?disableIcon:enableIcon).bind();
    glQuad(blit,vec2(0,0),vec2(size,size),true);
}
bool ToggleButton::mouseEvent(int2, Event event, Button button) {
    if(event==Press && button==LeftButton) { enabled = !enabled; toggled.emit(enabled); return true; }
    return false;
}
