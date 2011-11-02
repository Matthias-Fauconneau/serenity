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
	return int2(expanding?0:width,height+2*margin);
}

void Horizontal::update() {
	int width = Widget::size.x-2*margin, expanding=0;
	for(auto& child : *this) {
		int2 size=child.sizeHint();
		if(size.x) width -= size.x; else expanding++;
		width -= margin*2;
	}
	if(expanding) width /= expanding;
	int2 pen=int2(margin,margin);
	for(auto& child : *this) {
		int2 size=child.sizeHint();
		if(size.y==0) size.y=this->size.y-2*margin;
		if(size.x==0) size.x=width;
		child.size=size;
		child.position= int2(pen.x, pen.y+(this->size.y-2*margin-size.y)/2);
		child.update();
		pen.x += size.x + 2*margin;
	}
}

/// Vertical

int2 Vertical::sizeHint() {
	int width=0, expanding=0, height=0;
	for(auto& child : *this) {
		int2 size=child.sizeHint();
		height = max(height,size.x);
		if(size.y) width += size.y; else expanding++;
		width += margin*2;
	}
	return int2(height+2*margin,expanding?0:width);
}

void Vertical::update() {
	int width = size.y-2*margin, expanding=0;
	for(auto& child : *this) {
		int2 size=child.sizeHint();
		if(size.y) width -= size.y; else expanding++;
		width -= margin*2;
	}
	if(expanding) width /= expanding;
	int2 pen=int2(margin,margin);
	for(auto& child : *this) {
		int2 size=child.sizeHint();
		if(size.x==0) size.x=this->size.x-2*margin;
		if(size.y==0) size.y=width;
		child.size=size;
		child.position= int2(pen.x+(this->size.x-2*margin-size.x)/2, pen.y);
		child.update();
		pen.y += size.y + 2*margin;
	}
}

/// Text

Text::Text(int fontSize, string&& text) : fontSize(fontSize), font(Font::instance(fontSize)), text(move(text)) {}

int2 Text::sizeHint() {
	int widestLine = 0, width=0, height=font->height();
	trace_off
	for(char c: text) {
		if(c=='\n') { widestLine=max(width,widestLine); width=0; height+=font->height(); continue; }
		const Font::Glyph& glyph = font->glyph(c);
		width += glyph.advance.x;
	}
	trace_on
	widestLine=max(width,widestLine);
	return int2(widestLine,height);
}

void Text::render(vec2 scale, vec2 offset) {
	blit.bind(); blit["scale"]=scale; blit["offset"]=offset;
	int2 pen = int2(0,font->ascender());
	for(char c: text) {
		if(c=='\n') { pen.x=0; pen.y+=font->height(); continue; }
		Font::Glyph& glyph = font->glyph(c);
		if(glyph.texture) {
			glyph.texture.bind();
			glQuad(blit, vec2(pen+glyph.offset), vec2(pen+glyph.offset+glyph.size()), true);
		}
		pen.x += glyph.advance.x;
	}
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
		glQuad(flat,vec2(0,size.y),vec2(size));
	}
}

bool Slider::event(int2 position, int event, int state) {
	if((event == Motion || event==LeftButton) && state==Pressed) {
		value = minimum+position.x*(maximum-minimum)/size.x;
		emit(valueChanged,value);
	}
	return true;
}

/// List

/*List& operator <<(Text) {
	array<string> files = listFiles(path);
	for(auto& arg : files) {
		items << Text(move(arg),16);
	}
	for(auto& widget: items) *this << widget;
}*/

bool List::event(int2 position, int event, int state) {
	if(event != LeftButton || state != Pressed) return false;
	int i=0;
	for(auto& child: *this) {
		if(position>child.position-int2(margin,margin) && position<child.position+child.size+int2(margin,margin)) {
			if(i!=index) {
				index=i;
				emit(currentChanged,i);
			}
			return true;
		}
		i++;
	}
	return false;
}

void List::render(vec2 scale, vec2 offset) {
	Vertical::render(scale,offset);
	if(index<0) return;
	flat.bind(); flat["scale"]=scale; flat["offset"]=offset; flat["color"]=vec4(0,0.5,1,0.25);
	Widget& current = operator [](index);
	glQuad(flat,vec2(current.position-int2(margin,margin)), vec2(current.position+current.size+int2(margin,margin)));
}

/// Button //TODO: split TriggerButton | ToggleButton

void Button::setIcon(const Image& icon) { enable=icon; }
void Button::setCheckable(const Image& enableIcon, const Image& disableIcon) {
	enable=enableIcon, disable=disableIcon, toggle=false;
}

int2 Button::sizeHint() { return int2(size,size); }

void Button::render(vec2 scale, vec2 offset) {
	blit.bind(); blit["scale"]=scale; blit["offset"]=offset;
	(enabled && disable?disable:enable).bind();
	glQuad(blit,vec2(0,0),vec2(Widget::size),true);
	if(enabled && !disable) {
		flat.bind();
		flat["scale"]=scale; flat["offset"]=offset; flat["color"]=vec4(6.0/8,6.0/8,6.0/8,1);
		glQuad(flat,vec2(-1,-1),vec2(Widget::size+int2(1,1)));
	}
}

bool Button::event(int2, int event, int state) {
	if(event==LeftButton && state==Pressed) {
		if(toggle) { enabled = !enabled; emit(toggled,enabled); }
		else emit(triggered);
	}
	return true;
}
