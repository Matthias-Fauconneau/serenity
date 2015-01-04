#include "selection.h"

// Selection
bool Selection::mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus) {
    array<Rect> widgets = layout(size);
    if(event==Press) focus=this;
    for(uint i: range(widgets.size)) {
        if(widgets[i].contains(cursor)) {
			if(at(i).mouseEvent(cursor - widgets[i].origin(), widgets[i].size(), event, button, focus)) return true;
            if(event==Press && button == LeftButton) { setActive(i); return true; }
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
shared<Graphics> HighlightSelection::graphics(vec2 size, Rect clip) {
	shared<Graphics> graphics;
	graphics->graphics.insert(vec2(0), Selection::graphics(size, clip));
    if(index != invalid) {
        array<Rect> widgets = layout(size);
		graphics->fills.append(vec2(widgets[index].origin()), vec2(widgets[index].size()), lightBlue, 1.f/2);
    }
    return graphics;
}
