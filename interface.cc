#include "interface.h"
#include "font.h"
#include "gl.h"

/// ScrollArea

bool ScrollArea::event(int2 position, Event event, State state) {
    if(widget().event(position-widget().position,event,state)) return true;
    if((event==WheelDown || event==WheelUp) && state==Pressed) {
        if(size>=widget().sizeHint()) return false;
        int2& position = widget().position;
        int2 previous = position;
        position.y += event==WheelUp?-32:32;
        position = max(size-widget().sizeHint(),min(int2(0,0),position));
        if(position != previous) update();
        return true;
    }
    return false;
}

/// Layout

bool Layout::event(int2 position, Event event, State state) {
    for(auto& child : *this) {
        if(position > child.position && position < child.position+child.size) {
            if(child.event(position-child.position,event,state)) return true;
        }
    }
    return false;
}

void Layout::render(int2 parent) {
    for(auto& child : *this) {
        if(position+child.position>=int2(0,0) && child.position+child.size<=size)
            child.render(parent+position);
    }
}

/// Linear
//in sizeHint() and update(), xy() transform coordinates so that x/y always mean along/across the line

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
    array<int> sizes(count(),-1);
    int width = size.x /*remaining space*/, expanding=0 /*expanding count*/, expandingWidth=0;
    for(int p=0;p<2;p++) { //try hard to allocate space (i.e also allocate expanding widgets if bigger than (free space / expanding count))
        for(int i=0;i<count();i++) {
            if(sizes[i]>=0) continue;
            int hint = xy(at(i).sizeHint()).x;
            if(hint>=0 && hint >= width-expandingWidth) hint=-hint; //convert to expanding if not enough space
            if(hint>=0 && hint <= width-expandingWidth) { width -= sizes[i]=hint; assert(width>=0); continue; }
            if(p==0) { expanding++; expandingWidth+=-hint; } //count expanding widgets
            if(expanding <= 1) continue;
            if(-hint>width/expanding && -hint < width) { width -= sizes[i]=-hint; expanding--; expandingWidth-=-hint; }
            assert(width>=0);
        }
    }
    int margin = expanding ? width-width/expanding*expanding //integer rounding leave some unused margin
                           : width/(count()+1); //spread out all fixed size items
    int2 pen = int2(margin/2,0);
    for(int i=0;i<count();i++) {
        if(sizes[i]<0) sizes[i] = width/expanding;
        at(i).size = xy(int2(sizes[i],size.y));
        at(i).position = xy(pen);
        at(i).update();
        pen.x += sizes[i] + (expanding?0:margin);
    }
}

/// Text

Text::Text(int size, string&& text) : size(size), font(defaultFont) { setText(move(text)); }
void Text::setText(string&& t) {
    text=move(t);
    if(!text) return;
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
    assert(textSize);
    if(Widget::size) update();
}
int2 Text::sizeHint() { assert(textSize); return textSize; }

void Text::render(int2 parent) {
    blit.bind(); blit["offset"]=vec2(parent+position+(Widget::size-textSize)/2);
    for(const auto& b: blits) { GLTexture::bind(b.id); glQuad(blit, b.min, b.max, true); }
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

bool Slider::event(int2 position, Event event, State state) {
	if((event == Motion || event==LeftButton) && state==Pressed) {
		value = minimum+position.x*(maximum-minimum)/size.x;
		valueChanged.emit(value);
        return true;
	}
    return false;
}

/// Selection

bool Selection::event(int2 position, Event event, State state) {
    if(Layout::event(position,event,state)) return true;
	if(event != LeftButton || state != Pressed) return false;
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
    if(position+current.position>=int2(0,0) && current.position+current.size<=size) {
        flat.bind(); flat["offset"]=vec2(parent+position); flat["color"]=mix(vec4(0, 1./2, 1, 1),vec4(1,1,1,1),1./4);
        glQuad(flat,vec2(current.position), vec2(current.position+current.size));
    }
}

/// TriggerButton

TriggerButton::TriggerButton(const Image& icon) : icon(icon) {}
int2 TriggerButton::sizeHint() { return int2(icon.width,icon.height); }
void TriggerButton::render(int2 parent) {
    if(!icon) return;
    int size = min(Widget::size.x,Widget::size.y);
    int2 offset = (Widget::size-int2(size,size))/2;
    blit.bind(); blit["offset"]=vec2(parent+position+offset);
	icon.bind();
    glQuad(blit,vec2(0,0),vec2(size,size),true);
}
bool TriggerButton::event(int2, Event event, State state) {
    if(event==LeftButton && state==Pressed) { triggered.emit(); return true; }
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
bool ToggleButton::event(int2, Event event, State state) {
    if(event==LeftButton && state==Pressed) { enabled = !enabled; toggled.emit(enabled); return true; }
    return false;
}
