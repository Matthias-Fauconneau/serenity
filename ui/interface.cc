#include "interface.h"

// ScrollArea

shared<Graphics> ScrollArea::graphics(int2 size) {
	this->viewSize = size;
    int2 hint = abs(widget().sizeHint(int2( horizontal||size.x<0 ? 0/*-1 FIXME*/: 1, vertical||size.y<0? 0/*-1 FIXME*/: 1 )*abs(size)));
    int2 view (horizontal?max(hint.x,size.x):size.x, vertical?max(hint.y,size.y):size.y);
	assert_(offset <= vec2(0) && (!(size < view) || offset==vec2(0)), offset, view, size);
	shared<Graphics> graphics;
    graphics->graphics.insert(offset, widget().graphics(view, Rect::fromOriginAndSize(int2(-offset), size)));
	if(scrollbar) {
		if(size.y<view.y)
			graphics->fills.append( vec2(size.x-scrollBarWidth, -offset.y*size.y/view.y), vec2(scrollBarWidth, size.y*size.y/view.y), 1./2, 1.f/2);
		if(size.x<view.x)
			graphics->fills.append( vec2(-offset.x*size.x/view.x, size.y-scrollBarWidth), vec2(size.x*size.x/view.x, scrollBarWidth), 1./2, 1.f/2);
	}
    return graphics;
}

bool ScrollArea::mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) {
    //int2 hint = abs(widget().sizeHint(size));
    int2 hint = abs(widget().sizeHint(int2(horizontal||size.x<0?0/*-1 FIXME*/:1, vertical||size.y<0?0/*-1 FIXME*/:1)*abs(size)));
    if(event==Press) {
        focus = this;
		if((button==WheelDown || button==WheelUp)) for(int axis: range(2)) {
			if(size[axis]<hint[axis]) {
				offset[axis] = -stop(axis, -offset[axis], button==WheelDown?1:-1);
				offset = min(vec2(0), max(-vec2(hint-size), offset));
				return true;
			}
		}
		if(button==LeftButton) {
			if(scrollbar && button==LeftButton) for(int axis: range(2)) {
				if(size[axis]<hint[axis]) {
					if(cursor[!axis]>size[!axis]-scrollBarWidth) {
						offset[axis] = min(0, max(size[axis]-hint[axis], -cursor[axis]*(hint[axis]-size[axis]/2)/size[axis]));
						dragStartCursor=cursor, dragStartOffset=offset;
						return true;
					}
				}
			}
			dragStartCursor=cursor, dragStartOffset=offset;
		}
	}
	if(scrollbar && button==LeftButton && event==Motion) for(int axis: range(2)) {
		if(size[axis]<hint[axis] && dragStartCursor[!axis]>size[!axis]-scrollBarWidth) {
			offset[axis] = min(0.f, max<float>(-(hint[axis]-size[axis]), dragStartOffset[axis]-(cursor[axis]-dragStartCursor[axis])*hint[axis]/size[axis]));
			return true;
		}
	}
	if(widget().mouseEvent(cursor-int2(offset), max(hint,size), event, button, focus)) return true;
	if(event==Motion && button==LeftButton && !(hint<=size)) {
		offset = min(vec2(0), max(-vec2(hint-size), dragStartOffset+vec2(cursor-dragStartCursor)));
        return true;
    }
    return false;
}

bool ScrollArea::keyPress(Key key, Modifiers) {
    int2 size = viewSize;
    int2 hint = abs(widget().sizeHint(int2( horizontal||size.x<0 ? 0/*-1 FIXME*/: 1, vertical||size.y<0? 0/*-1 FIXME*/: 1 )*abs(size)));
    int2 view (horizontal?max(hint.x,size.x):size.x, vertical?max(hint.y,size.y):size.y);

    if(key==Home) { offset = 0; return true; }
	if(key==End) { offset = -vec2(hint-viewSize); return true; }
    if(key==Space || key==LeftArrow || key==RightArrow || key==Backspace) for(int axis: range(2)) {
        if(viewSize[axis]<hint[axis]) {
            offset[axis] = -widget().stop(view[axis], axis, -offset[axis], key==LeftArrow || key==Backspace?-1:1);
            offset = min(vec2(0), max(-vec2(hint-size), offset));
            return true;
        }
    }

	return false;
}

// Progress

int2 Progress::sizeHint(int2) { return int2(-height,height); }
shared<Graphics> Progress::graphics(int2 size) {
	shared<Graphics> graphics;
	assert(minimum <= value && value <= maximum, minimum, value, maximum);
    int x = size.x*uint(value-minimum)/uint(maximum-minimum);
	graphics->fills.append(vec2(0,1), vec2(x,size.y-1-1), lightBlue, 1.f);
	graphics->fills.append(vec2(x,1), vec2(size.x-x,size.y-1-1), gray, 1.f);
    return graphics;
}

// ImageView

int2 ImageView::sizeHint(int2 size) { return min(image.size, size.x && image.size.x ? image.size*size.x/image.size.x : image.size); }

shared<Graphics> ImageView::graphics(int2 size) {
	shared<Graphics> graphics;
    if(image) {
		// Fit
		/*int2 target = min(image.size*size.x/image.size.x, image.size*size.y/image.size.y);
		graphics->blits.append(max(vec2(0),vec2((size-target)/2)), vec2(target), share(image));*/
		// Crop
		//int2 offset = size-image.size;
		graphics->blits.append(
					max(vec2(0),vec2((size-image.size)/2)), // Centers
					vec2(min(size, image.size)), // or fits
					cropShare(image, max(int2(0),int2((image.size-size)/2)), min(size, image.size)) // by cropping center
					);
    }
    return graphics;
}

// Slider

bool Slider::mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*&) {
    if((event == Motion || event==Press) && button==LeftButton) {
        value = minimum+cursor.x*uint(maximum-minimum)/size.x;
        if(valueChanged) valueChanged(value);
        return true;
    }
    return false;
}

// ImageLink

bool ImageLink::mouseEvent(int2, int2, Event event, Button, Widget*&) {
    if(event==Press) { if(triggered) triggered(); return true; }
    return false;
}

//  ToggleButton
bool ToggleButton::mouseEvent(int2, int2, Event event, Button button, Widget*&) {
    if(event==Press && button==LeftButton) { enabled = !enabled; image = enabled?share(disableIcon):share(enableIcon); toggled(enabled); return true; }
    return false;
}

// WidgetCycle
bool WidgetCycle::mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) {
	focus = this;
	return widgets[index]->mouseEvent(cursor, size, event, button, focus);
}
bool WidgetCycle::keyPress(Key key, Modifiers modifiers) {
    size_t previousIndex = index;
	if(key == Return || key == Space || key == PageUp) index = (index+1)%widgets.size;
	if(key == Backspace || key == PageDown) index = (index+widgets.size-1)%widgets.size;
    return widgets[index]->keyPress(key, modifiers) || previousIndex != index;
}
