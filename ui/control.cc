#include "control.h"

// Progress
int2 Progress::sizeHint() { return int2(-height,height); }
void Progress::render() {
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
void HighlightSelection::render() {
    array<Rect> widgets = layout(target.size());
    for(uint i: range(count())) {
        if(i==index && (always || hasFocus(this) || hasFocus(&at(i)))) fill(target, widgets[i], highlight);
        at(i).render(clip(target, widgets[i]));
    }
}

// TabSelection
void TabSelection::render() {
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

// ImageLink
bool ImageLink::mouseEvent(int2, int2, Event event, Button) {
    if(event==Press) { if(triggered) triggered(); linkActivated(link); return true; }
    return false;
}

//  ToggleButton
int2 ToggleButton::sizeHint() { return (enabled?disableIcon:enableIcon).size(); }
void ToggleButton::render() {
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
