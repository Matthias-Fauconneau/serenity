#include "interface.h"
#include "display.h"
#include "layout.h"

/// ScrollArea

void ScrollArea::update() {
    int2 hint = abs(widget().sizeHint());
    if(size.x > hint.x) hint.x=size.x; //widget().position.x = (size.x-hint.x)/2;
    else widget().position.x = min(0, max(size.x-hint.x, widget().position.x));
    if(size.y > hint.y) hint.y=size.y; //widget().position.y = (size.y-hint.y)/2;
    else widget().position.y = min(0, max(size.y-hint.y, widget().position.y));
    widget().size = int2(hint.x,hint.y);
    widget().update();
}

bool ScrollArea::mouseEvent(int2 cursor, Event event, Button button) {
    if(event==Press && (button==WheelDown || button==WheelUp) && size.y<abs(widget().sizeHint().y)) {
        int2& position = widget().position;
        position.y += button==WheelUp?-32:32;
        position.y = max(size.y-abs(widget().sizeHint().y),min(0,position.y));
        return true;
    }
    if(widget().mouseEvent(cursor-widget().position,event,button)) return true;
    static int dragStart=0, flickStart=0;
    if(event==Press && button==LeftButton) {
        dragStart=cursor.y, flickStart=widget().position.y;
    }
    if(event==Motion && button==LeftButton && size.y<abs(widget().sizeHint().y)) {
        int2& position = widget().position;
        position.y = flickStart+cursor.y-dragStart;
        position.y = max(size.y-abs(widget().sizeHint().y),min(0,position.y));
        return true;
    }
    return false;
}

void ScrollArea::ensureVisible(Widget& target) {
    widget().position = max(-target.position, min(size-(target.position+target.size), widget().position));
}

/// Slider

int2 Slider::sizeHint() { return int2(-height,height); }

void Slider::render(int2 parent) {
    if(maximum > minimum && value >= minimum && value <= maximum) {
        int x = size.x*uint(value-minimum)/uint(maximum-minimum);
        fill(parent+position+Rect(int2(0,0), int2(x,size.y)), gray);
        fill(parent+position+Rect(int2(x,0), size), lightGray);
    } else {
        fill(parent+position+Rect(int2(0,0), size), gray);
    }
}

bool Slider::mouseEvent(int2 position, Event event, Button button) {
    if((event == Motion || event==Press) && button==LeftButton) {
        value = minimum+position.x*uint(maximum-minimum)/size.x;
        valueChanged(value);
        return true;
    }
    return false;
}

/// Selection

bool Selection::mouseEvent(int2 position, Event event, Button button) {
    if(Layout::mouseEvent(position,event,button)) return true;
    if(event != Press) return false;
    if(button == WheelDown && index>0 && index<count()) { index--; at(index).selectEvent(); activeChanged(index); return true; }
    if(button == WheelUp && index<count()-1) { index++; at(index).selectEvent(); activeChanged(index); return true; }
    focus=this;
    for(uint i=0;i<count();i++) { Widget& child=at(i);
        if(position>=child.position && position<=child.position+child.size) {
            if(index!=i) { index=i; at(index).selectEvent(); activeChanged(index); }
            if(button == LeftButton) itemPressed(index);
            return true;
        }
    }
    return false;
}

bool Selection::keyPress(Key key) {
    if(key==DownArrow && index<count()-1) { index++; at(index).selectEvent(); activeChanged(index); return true; }
    if(key==UpArrow && index>0 && index<count()) { index--; at(index).selectEvent(); activeChanged(index); return true; }
    return false;
}

void Selection::setActive(uint i) {
    assert(i==uint(-1) || i<count());
    if(index!=i) { index=i; if(index!=uint(-1)) { at(index).selectEvent(); activeChanged(index); } }
}

/// HighlightSelection

void HighlightSelection::render(int2 parent) {
    if(index<count()) {
        Widget& current = at(index);
        if(position+current.position>=int2(-4,-4) && current.position+current.size<=(size+int2(4,4))) {
            fill(parent+position+current.position+Rect(current.size), selectionColor);
        }
    }
    Layout::render(parent);
}

/// TabSelection

void TabSelection::render(int2 parent) {
    if(index==uint(-1)) { fill(parent+position+Rect(size), gray); Layout::render(parent); return; } //no active tab
    Widget& current = at(index);
    if(index>0) fill(parent+position+Rect(int2(current.position.x, size.y)), gray); //dark inactive tabs before current
    fill(parent+position+current.position+Rect(int2(current.size.x,size.y)), lightGray); //light active tab
    if(index<count()-1) fill(parent+position+Rect(int2(current.position.x+current.size.x, 0), size), gray); //dark inactive tabs after current
    Layout::render(parent);
}

/// ImageView
int2 ImageView::sizeHint() { return image.size(); }
void ImageView::render(int2 parent) {
    if(!image) return;
    int2 pos = parent+position+(Widget::size-image.size())/2;
    blit(pos, image);
}

/// TriggerButton

bool TriggerButton::mouseEvent(int2, Event event, Button) {
    if(event==Press) { triggered(); return true; }
    return false;
}

///  ToggleButton
int2 ToggleButton::sizeHint() { return (enabled?disableIcon:enableIcon).size(); }
void ToggleButton::render(int2 parent) {
    Image<byte4>& image = enabled?disableIcon:enableIcon;
    if(!image) return;
    int2 pos = parent+position+(Widget::size-image.size())/2;
    blit(pos, image);
}
bool ToggleButton::mouseEvent(int2, Event event, Button button) {
    if(event==Press && button==LeftButton) { enabled = !enabled; toggled(enabled); return true; }
    return false;
}
