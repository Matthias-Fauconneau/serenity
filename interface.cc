#include "interface.h"
#include "display.h"
#include "layout.h"

/// ScrollArea

void ScrollArea::render(int2 position, int2 size) {
    int2 hint = abs(widget().sizeHint());
    delta = min(int2(0,0), max(size-hint, delta));
    widget().render(position+delta, max(hint,size));
}

bool ScrollArea::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    int2 hint = abs(widget().sizeHint());
    if(event==Press && (button==WheelDown || button==WheelUp) && size.y<hint.y) {
        delta.y += button==WheelUp?-32:32;
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

//void ScrollArea::ensureVisible(Widget& target) { delta = max(-target.position, min(size-(target.position+target.size), widget().position)); }

/// Slider

int2 Slider::sizeHint() { return int2(-height,height); }

void Slider::render(int2 position, int2 size) {
    if(maximum > minimum && value >= minimum && value <= maximum) {
        int x = size.x*uint(value-minimum)/uint(maximum-minimum);
        fill(position+Rect(int2(0,0), int2(x,size.y)), darken);
        fill(position+Rect(int2(x,0), size), lighten);
    } else {
        fill(position+Rect(int2(0,0), size), darken);
    }
}

bool Slider::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    if((event == Motion || event==Press) && button==LeftButton) {
        value = minimum+cursor.x*uint(maximum-minimum)/size.x;
        valueChanged(value);
        return true;
    }
    return false;
}

/// Selection

bool Selection::mouseEvent(int2 cursor, int2 unused size, Event event, Button button) {
    array<Rect> widgets = layout(int2(0,0), size);
    for(uint i=0;i<widgets.size();i++) {
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
    for(uint i=0;i<count();i++) {
        if(i==index) fill(widgets[i], highlight);
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
    for(uint i=0;i<count();i++) at(i).render(widgets[i]);
}

/// ImageView
int2 ImageView::sizeHint() { return image.size(); }
void ImageView::render(int2 position, int2 size) {
    if(!image) return;
    int2 pos = position+(size-image.size())/2;
    blit(pos, image);
}

/// TriggerButton

bool TriggerButton::mouseEvent(int2, int2, Event event, Button) {
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
bool ToggleButton::mouseEvent(int2, int2, Event event, Button button) {
    if(event==Press && button==LeftButton) { enabled = !enabled; toggled(enabled); return true; }
    return false;
}
