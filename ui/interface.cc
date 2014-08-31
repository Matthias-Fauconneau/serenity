#include "interface.h"
#include "layout.h"
#include "text.h"
#include "graphics.h"

// ScrollArea

Graphics ScrollArea::graphics(int2 size) const {
    int2 hint = abs(widget().sizeHint(size));
    int2 view (horizontal?max(hint.x,size.x):size.x,vertical?max(hint.y,size.y):size.y);
    if(view <= size) return widget().graphics(size);
    else return move(Graphics().append(widget().graphics(view), vec2(offset)));
    //if(scrollbar && size.y<view.y) fill(target, Rect(int2(size.x-scrollBarWidth, -offset.y*size.y/view.y), int2(size.x,(-offset.y+size.y)*size.y/view.y)), 0.5);
}

bool ScrollArea::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    int2 hint = abs(widget().sizeHint(size));
    if(event==Press && (button==WheelDown || button==WheelUp) && size.y<hint.y) {
        offset.y += (button==WheelUp?1:-1) * 64;
        offset = min(int2(0,0), max(size-hint, offset));
        setFocus(this);
        return true;
    }
    if(event==Press && button==LeftButton) { dragStartCursor=cursor, dragStartDelta=offset; setFocus(this); }
    if(event==Motion && button==LeftButton && size.y<hint.y && scrollbar && dragStartCursor.x>size.x-scrollBarWidth) {
        offset.y = min(0, max(size.y-hint.y, dragStartDelta.y-(cursor.y-dragStartCursor.y)*hint.y/size.y));
        return true;
    }
    if(widget().mouseEvent(cursor-offset,max(hint,size),event,button)) return true;
    if(event==Motion && button==LeftButton && size.y<hint.y) {
        setDrag(this);
        offset = min(int2(0,0), max(size-hint, dragStartDelta+cursor-dragStartCursor));
        return true;
    }
    return false;
}

bool ScrollArea::keyPress(Key key, Modifiers) {
    int2 hint = abs(widget().sizeHint(size));
    if(key==PageUp || key==PageDown) { offset.y += (key==PageUp?1:-1) * size.y; offset = min(int2(0,0), max(size-hint, offset)); return true; }
    return false;
}

void ScrollArea::ensureVisible(Rect target) { offset = max(-target.min, min(size-target.max, offset)); }

void ScrollArea::center(int2 target) { offset = size/2-target; }

// ImageWidget

int2 ImageWidget::sizeHint(int2 size) const {
    int2 target = min(image.size*size.x/image.size.x, image.size/**size.y/image.size.y*/);
    return min(image.size, target);
}

Graphics ImageWidget::graphics(int2 size) const {
    Graphics graphics;
    if(image) {
        int2 target = min(image.size*size.x/image.size.x, image.size*size.y/image.size.y);
        graphics.blits.append(Blit{vec2(max(vec2(0),vec2((size-target)/2))), vec2(target), share(image)});
    }
    return graphics;
}
