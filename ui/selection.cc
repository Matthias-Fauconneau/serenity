#include "selection.h"

// Selection
bool Selection::mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) {
    array<Rect> widgets = layout(size);
    if(event==Press) focus=this;
    for(uint i: range(widgets.size)) {
        if(widgets[i].contains(cursor)) {
            if(at(i).mouseEvent(cursor-widgets[i].origin,widgets[i].size,event,button,focus)) return true;
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
Graphics HighlightSelection::graphics(int2 size) const {
    Graphics graphics = Selection::graphics(size);
    if(index != invalid) {
        array<Rect> widgets = layout(size);
        graphics.fills << Fill{vec2(widgets[index].origin), vec2(widgets[index].size), lightBlue, 1./2};
    }
    return graphics;
}
