#include "interface.h"
#include "display.h"
#include "layout.h"

/// ScrollArea

void ScrollArea::render(int2 position, int2 size) {
    this->size=size;
    int2 hint = abs(widget().sizeHint());
    delta = min(int2(0,0), max(size-hint, delta));
    widget().render(position+delta, max(hint,size));
}

bool ScrollArea::mouseEvent(int2 cursor, int2 size, Event event, MouseButton button) {
    int2 hint = abs(widget().sizeHint());
    if(event==Press && (button==WheelDown || button==WheelUp) && size.y<hint.y) {
        delta.y += button==WheelUp?-64:64;
        delta = min(int2(0,0), max(size-hint, delta));
        return true;
    }
    if(event==Press && button==LeftButton) { dragStart=cursor, flickStart=delta; }
    if(widget().mouseEvent(cursor-delta,max(hint,size),event,button)) return true;
    if(event==Motion && button==LeftButton && size.y<hint.y) {
        drag=this;
        delta = min(int2(0,0), max(size-hint, flickStart+cursor-dragStart));
        return true;
    }
    return false;
}

void ScrollArea::ensureVisible(Rect target) { delta = max(-target.min, min(size-target.max, delta)); }
void ScrollArea::center(int2 target) { delta = size/2-target; }

/// Progress

int2 Progress::sizeHint() { return int2(-height,height); }

void Progress::render(int2 position, int2 size) {
    if(maximum > minimum && value >= minimum && value <= maximum) {
        int x = size.x*uint(value-minimum)/uint(maximum-minimum);
        fill(position+Rect(int2(0,1), int2(x,size.y-2)), lighten);
        fill(position+Rect(int2(x,1), int2(size.x,size.y-2)), darken);
    } else {
        fill(position+Rect(0, size), darken);
    }
}

/// Slider

bool Slider::mouseEvent(int2 cursor, int2 size, Event event, MouseButton button) {
    if((event == Motion || event==Press) && button==LeftButton) {
        value = minimum+cursor.x*uint(maximum-minimum)/size.x;
        valueChanged(value);
        return true;
    }
    return false;
}

/// Selection

bool Selection::mouseEvent(int2 cursor, int2 unused size, Event event, MouseButton button) {
    array<Rect> widgets = layout(0, size);
    for(uint i: range(widgets.size())) {
        if(widgets[i].contains(cursor)) {
            if(at(i).mouseEvent(widgets[i],cursor,event,button)) return true;
            if(event==Press && button == LeftButton) {
                focus=this;
                setActive(i);
                itemPressed(index);
                return true;
            }
        }
    }
    if(button == WheelDown && index>0 && index<count()) { setActive(index-1); return true; }
    if(button == WheelUp && index<count()-1) { setActive(index+1); return true; }
    return false;
}

bool Selection::keyPress(Key key) {
    if(index<=count()) if(at(index).keyPress(key)) return true;
    if(key==DownArrow && index<count()-1) { setActive(index+1); return true; }
    if(key==UpArrow && index>0 && index<count()) { setActive(index-1); return true; }
    return false;
}

void Selection::setActive(uint i) {
    assert(i==uint(-1) || i<count());
    index=i; if(index!=uint(-1)) activeChanged(index);
}

/// HighlightSelection

void HighlightSelection::render(int2 position, int2 size) {
    array<Rect> widgets = layout(position, size);
    for(uint i: range(count())) {
        if(i==index && (always || focus==this || focus==&at(i))) fill(widgets[i], highlight);
        at(i).render(widgets[i]);
    }
}

/// TabSelection

void TabSelection::render(int2 position, int2 size) {
    array<Rect> widgets = layout(position, size);
    if(index>=count()) fill(position+Rect(size), darken); //no active tab
    else {
        Rect active = widgets[index];
        if(index>0) fill(Rect(position, int2(active.min.x, position.y+size.y)), darken); //darken inactive tabs before current
        //fill(active, lightGray); //light active tab
        if(index<count()-1) fill(Rect(int2(active.max.x,position.y), position+size), darken); //darken inactive tabs after current
    }
    for(uint i: range(count()))  at(i).render(widgets[i]);
}

/// ImageView
int2 ImageView::sizeHint() { return image.size(); }
void ImageView::render(int2 position, int2 size) {
    if(!image) return;
    int2 pos = position+(size-image.size())/2;
    blit(pos, image);
}

/// TriggerButton

bool TriggerButton::mouseEvent(int2, int2, Event event, MouseButton) {
    if(event==Press) { triggered(); return true; }
    return false;
}

///  ToggleButton
int2 ToggleButton::sizeHint() { return (enabled?disableIcon:enableIcon).size(); }
void ToggleButton::render(int2 position, int2 size) {
    Image& image = enabled?disableIcon:enableIcon;
    if(!image) return;
    int2 pos = position+(size-image.size())/2;
    blit(pos, image);
}
bool ToggleButton::mouseEvent(int2, int2, Event event, MouseButton button) {
    if(event==Press && button==LeftButton) { enabled = !enabled; toggled(enabled); return true; }
    return false;
}

/// TriggerItem

bool TriggerItem::mouseEvent(int2, int2, Event event, MouseButton) {
    if(event==Press) { triggered(); return true; }
    return false;
}
