#include "interface.h"
#include "font.h"
#include "gl.h"

/// Horizontal

int2 Horizontal::sizeHint() {
	int width=0, expanding=0, height=0;
	for(auto& child : *this) {
		int2 size=child.sizeHint();
		height = max(height,size.y);
		if(size.x) width += size.x; else expanding++;
		width += margin*2;
	}
	return int2(0,height+2*margin);
}

void Horizontal::update() {
	int width = Widget::size.x-2*margin, expanding=0;
	for(auto& child : *this) {
		int2 size=child.sizeHint();
		if(size.x) width -= size.x; else expanding++;
		width -= margin*2;
	}
	if(expanding) width /= expanding; //divide free space between expanding widgets
	else width /= count(); //otherwise spread out widgets
	int2 pen=int2(margin+(expanding?0:width)/2,margin);
	for(auto& child : *this) {
		int2 size=child.sizeHint();
		if(size.x==0||size.x>this->size.x-2*margin) size.x=width;
		if(size.y==0||size.y>this->size.y-2*margin) size.y=this->size.y-2*margin;
		child.size=size;
		child.position= int2(pen.x, pen.y+(this->size.y-2*margin-size.y)/2);
		child.update();
		pen.x += size.x + 2*margin + (expanding?0:width);
	}
}

/// Vertical

int2 Vertical::sizeHint() {
	int height=0, expanding=0, width=0;
	for(auto& child : *this) {
		int2 size=child.sizeHint();
		width = max(width,size.x);
		if(size.y) height += size.y; else expanding++;
		height += margin*2;
	}
	return int2(width+2*margin,expanding?0:height);
}

void Vertical::update() {
	// compute total size
	int height = size.y-2*margin, expanding=0;
	for(auto& child : *this) {
		int2 size=child.sizeHint();
		if(size.y&&size.y<this->size.y) height -= size.y; else expanding++;
		height -= margin*2;
	}
	if(expanding) height /= expanding;

	int2 pen=int2(margin,margin-scroll);
	first=-1; last=-1;
	for(int i=0;i<count();i++) {
		auto& child=at(i);
		int2 size=child.sizeHint();
		if(size.x==0||size.x>this->size.x-2*margin) size.x=this->size.x-2*margin;
		if(size.y==0||size.y>this->size.y-2*margin) size.y=height;
		child.size = size;
		child.position= int2(pen.x+((this->size.x-2*margin)-size.x)/2, pen.y);
		child.update();
		if(first==-1 && pen.y>=0) first=i;
		if(pen.y+size.y<=this->size.y) last = i+1;
		pen.y += size.y + 2*margin;
	}
}

void Vertical::render(vec2 scale, vec2 offset) {
	for(int i=first;i<last;i++) at(i).render(scale,offset+vec2(at(i).position)*scale);
}

bool Vertical::event(int2 position, int event, int state) {
	if(Layout::event(position,event,state)) return true;
	if(!mayScroll || sizeHint().y<=size.y) return false;
	if(event==WheelDown && state==Pressed) { scroll=clip(0,scroll-at(first).size.y,sizeHint().y-size.y); update(); return true; }
	if(event==WheelUp && state==Pressed) { scroll=clip(0,scroll+at(last).size.y,sizeHint().y-size.y); update(); return true; }
	return false;
}

/// Text

Text::Text(int size, string&& text) : size(size), font(defaultFont) { setText(move(text)); }
void Text::setText(string&& text) {
	if(!text.size) { this->text=move(text); return; }
	int widestLine = 0;
	FontMetrics metrics = font.metrics(size*64);
	vec2 pen = vec2(0,metrics.ascender);
	blits.clear();
	char previous=0;
	for(char c: text) {
		if(c=='\n') { widestLine=max(int(pen.x),widestLine); pen.x=0; pen.y+=metrics.height; continue; }
		const Glyph& glyph = font.glyph(size*64,c);
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
	this->text=move(text);
}

int2 Text::sizeHint() { return textSize; }

void Text::render(vec2 scale, vec2 offset) {
	blit.bind(); blit["scale"]=scale; blit["offset"]=offset;
	for(const auto& b: blits) { GLTexture::bind(b.id); glQuad(blit, b.min, b.max, true); }
}

/// Slider

int2 Slider::sizeHint() { return int2(0,height); }

void Slider::render(vec2 scale, vec2 offset) {
	flat.bind();
	flat["scale"]=scale; flat["offset"]=offset; flat["color"]=vec4(4.0/8,4.0/8,4.0/8,1);
	if(maximum > minimum && value >= minimum && value <= maximum) {
		int x = size.x*(value-minimum)/(maximum-minimum);
		glQuad(flat,vec2(0,0),vec2(x,size.y));
		flat["color"]=vec4(6.0/8,6.0/8,6.0/8,1);
		glQuad(flat,vec2(x,0),vec2(size));
	} else {
		glQuad(flat,vec2(0,0),vec2(size));
	}
}

bool Slider::event(int2 position, int event, int state) {
	if((event == Motion || event==LeftButton) && state==Pressed) {
		value = minimum+position.x*(maximum-minimum)/size.x;
		valueChanged.emit(value);
	}
	return true;
}

/// List

bool List::event(int2 position, int event, int state) {
	if(Vertical::event(position,event,state)) return true;
	if(event != LeftButton || state != Pressed) return false;
	int i=0;
	for(auto& child: *this) {
		if(position>child.position-int2(margin,margin) && position<child.position+child.size+int2(margin,margin)) {
			if(i!=index) {
				index=i;
				currentChanged.emit(i);
			}
			return true;
		}
		i++;
	}
	return false;
}

void List::render(vec2 scale, vec2 offset) {
	Vertical::render(scale,offset);
	if(index<first || index>=last) return;
	flat.bind(); flat["scale"]=scale; flat["offset"]=offset; flat["color"]=mix(vec4(0,0.5,1,1),vec4(1,1,1,1),0.25);
	Widget& current = at(index);
	glQuad(flat,vec2(current.position-int2(margin,margin)), vec2(current.position+current.size+int2(margin,margin)));
}

/// TriggerButton

TriggerButton::TriggerButton(const Image& icon) : icon(icon) {}
int2 TriggerButton::sizeHint() { return int2(size,size); }
void TriggerButton::render(vec2 scale, vec2 offset) {
	blit.bind(); blit["scale"]=scale; blit["offset"]=offset;
	icon.bind();
	glQuad(blit,vec2(0,0),vec2(Widget::size),true);
}
bool TriggerButton::event(int2, int event, int state) {
	if(event==LeftButton && state==Pressed) triggered.emit();
	return true;
}

///  ToggleButton

ToggleButton::ToggleButton(const Image& enableIcon, const Image& disableIcon) : enableIcon(enableIcon), disableIcon(disableIcon) {}
int2 ToggleButton::sizeHint() { return int2(size,size); }
void ToggleButton::render(vec2 scale, vec2 offset) {
	blit.bind(); blit["scale"]=scale; blit["offset"]=offset;
	(enabled?disableIcon:enableIcon).bind();
	glQuad(blit,vec2(0,0),vec2(Widget::size),true);
}
bool ToggleButton::event(int2, int event, int state) {
	if(event==LeftButton && state==Pressed) { enabled = !enabled; toggled.emit(enabled); }
	return true;
}
