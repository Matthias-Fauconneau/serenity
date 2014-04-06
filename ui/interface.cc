#include "interface.h"
#include "layout.h"
#include "text.h"
#include "graphics.h"


// ScrollArea
void ScrollArea::render(const Image& target) {
    size=target.size();
    int2 hint = abs(widget().sizeHint());
    int2 view (horizontal?max(hint.x,size.x):size.x,vertical?max(hint.y,size.y):size.y);
    if(view <= size) widget().render(target);
    else widget().render(target, offset, view);
    if(scrollbar && size.y<view.y) fill(target, Rect(int2(size.x-scrollBarWidth, -offset.y*size.y/view.y), int2(size.x,(-offset.y+size.y)*size.y/view.y)), 0.5);
}
bool ScrollArea::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    int2 hint = abs(widget().sizeHint());
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
    int2 hint = abs(widget().sizeHint());
    if(key==PageUp || key==PageDown) { offset.y += (key==PageUp?1:-1) * size.y; offset = min(int2(0,0), max(size-hint, offset)); return true; }
    return false;
}
void ScrollArea::ensureVisible(Rect target) { offset = max(-target.min, min(size-target.max, offset)); }
void ScrollArea::center(int2 target) { offset = size/2-target; }

// Progress
int2 Progress::sizeHint() { return int2(-height,height); }
void Progress::render(const Image& target) {
    if(maximum > minimum && value >= minimum && value <= maximum) {
        int x = target.size().x*uint(value-minimum)/uint(maximum-minimum);
        fill(target, Rect(int2(0,1), int2(x,target.size().y-2)), highlight);
        fill(target, Rect(int2(x,1), int2(target.size().x,target.size().y-2)), darkGray);
    } else {
        fill(target, Rect(target.size()), darkGray);
    }
}

// Slider
bool Slider::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    if((event == Motion || event==Press) && button==LeftButton) {
        value = minimum+cursor.x*uint(maximum-minimum)/size.x;
        if(valueChanged) valueChanged(value);
        return true;
    }
    return false;
}

// Selection
bool Selection::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    array<Rect> widgets = layout(size);
    if(event==Press) setFocus(this);
    for(uint i: range(widgets.size)) {
        if(widgets[i].contains(cursor)) {
            if(at(i).mouseEvent(widgets[i],cursor,event,button)) return true;
            if(event==Press && button == LeftButton) {
                setActive(i);
                itemPressed(index);
                return true;
            }
        }
    }
    if(button == WheelUp && index>0 && index<count()) { setActive(index-1); return true; }
    if(button == WheelDown && index<count()-1) { setActive(index+1); return true; }
    return false;
}
bool Selection::keyPress(Key key, Modifiers modifiers) {
    if(index<=count()) if(at(index).keyPress(key, modifiers)) return true;
    if(key==DownArrow && index<count()-1) { setActive(index+1); return true; }
    if(key==UpArrow && index>0 && index<count()) { setActive(index-1); return true; }
    return false;
}
void Selection::setActive(uint i) {
    assert(i==uint(-1) || i<count());
    index=i; if(index!=uint(-1)) if(activeChanged) activeChanged(index);
}

// HighlightSelection
void HighlightSelection::render(const Image& target) {
    array<Rect> widgets = layout(target.size());
    for(uint i: range(count())) {
        if(i==index && (always || hasFocus(this) || hasFocus(&at(i)))) fill(target, widgets[i], highlight);
        at(i).render(clip(target, widgets[i]));
    }
}

// TabSelection
void TabSelection::render(const Image& target) {
    array<Rect> widgets = layout(target.size());
    if(index>=count()) fill(target, Rect(target.size()), darkGray); //no active tab
    else {
        Rect active = widgets[index];
        if(index>0) fill(target, Rect(int2(active.min.x, target.size().y)), darkGray); //darken inactive tabs before current
        fill(target, active, lightGray); //light active tab
        if(index<count()-1) fill(target, Rect(int2(active.max.x,0), target.size()), darkGray); //darken inactive tabs after current
    }
    for(uint i: range(count()))  at(i).render(clip(target, widgets[i]));
}

// ImageWidget
int2 ImageWidget::sizeHint() { return hidden ? 0 : image.size(); }
void ImageWidget::render(const Image& target) {
    if(!image) return;
    int2 offset = (target.size()-image.size())/2;
    blit(target, offset, image);
}

// ImageLink
bool ImageLink::mouseEvent(int2, int2, Event event, Button) {
    if(event==Press) { if(triggered) triggered(); linkActivated(link); return true; }
    return false;
}

//  ToggleButton
int2 ToggleButton::sizeHint() { return (enabled?disableIcon:enableIcon).size(); }
void ToggleButton::render(const Image& target) {
    const Image& image = enabled?disableIcon:enableIcon;
    if(!image) return;
    int2 offset = (target.size()-image.size())/2;
    blit(target, offset, image);
}
bool ToggleButton::mouseEvent(int2, int2, Event event, Button button) {
    if(event==Press && button==LeftButton) { enabled = !enabled; toggled(enabled); return true; }
    return false;
}

// TriggerItem
bool TriggerItem::mouseEvent(int2, int2, Event event, Button) {
    if(event==Press) { triggered(); return true; }
    return false;
}
