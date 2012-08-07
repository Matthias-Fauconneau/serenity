#include "interface.h"
#include "display.h"
#include "layout.h"
#include "array.cc"

Widget* focus;

/// ScrollArea

void ScrollArea::update() {
    int2 hint = abs(widget().sizeHint());
    if(size.x > hint.x) widget().position.x = (size.x-hint.x)/2;
    else widget().position.x = min(0, max(size.x-hint.x, widget().position.x));
    if(size.y > hint.y) widget().position.y = (size.y-hint.y)/2;
    else widget().position.y = min(0, max(size.y-hint.y, widget().position.y));
    widget().size = int2(hint.x,hint.y);
    widget().update();
}

bool ScrollArea::mouseEvent(int2 cursor, Event event, Key button) {
    if(event==Press && (button==Key::WheelDown || button==Key::WheelUp) && size.y<abs(widget().sizeHint().y)) {
        int2& position = widget().position;
        position.y += button==Key::WheelUp?-32:32;
        position.y = max(size.y-abs(widget().sizeHint().y),min(0,position.y));
        return true;
    }
    if(widget().mouseEvent(cursor-widget().position,event,button)) return true;
    static int dragStart=0, flickStart=0;
    if(event==Press && button==Key::Left) {
        dragStart=cursor.y, flickStart=widget().position.y;
    }
    if(event==Motion && button==Key::Left && size.y<abs(widget().sizeHint().y)) {
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
        fill(parent+position+Rect(int2(0,0), int2(x,size.y)), 128);
        fill(parent+position+Rect(int2(x,0), size), 192);
    } else {
        fill(parent+position+Rect(int2(0,0), size), 128);
    }
}

bool Slider::mouseEvent(int2 position, Event event, Key button) {
    if((event == Motion || event==Press) && button==Key::Left) {
        value = minimum+position.x*uint(maximum-minimum)/size.x;
        valueChanged(value);
        return true;
    }
    return false;
}

/// Selection

bool Selection::mouseEvent(int2 position, Event event, Key button) {
    if(Layout::mouseEvent(position,event,button)) return true;
    if(event != Press) return false;
    if(button == Key::WheelDown && index>0 && index<count()) { index--; at(index).selectEvent(); activeChanged(index); return true; }
    if(button == Key::WheelUp && index<count()-1) { index++; at(index).selectEvent(); activeChanged(index); return true; }
    focus=this;
    for(uint i=0;i<count();i++) { Widget& child=at(i);
        if(position>=child.position && position<=child.position+child.size) {
            if(index!=i) { index=i; at(index).selectEvent(); activeChanged(index); }
            if(button == Key::Left) itemPressed(index);
            return true;
        }
    }
    return false;
}

bool Selection::keyPress(Key key) {
    if(key==Key::Down && index<count()-1) { index++; at(index).selectEvent(); activeChanged(index); return true; }
    if(key==Key::Up && index>0 && index<count()) { index--; at(index).selectEvent(); activeChanged(index); return true; }
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
            fill(parent+position+current.position+Rect(current.size), pixel(224, 192, 128));
            //FIXME: fast text rendering doesn't coverage blend anymore, change text background color
        }
    }
    Layout::render(parent);
}

/// TabSelection

void TabSelection::render(int2 parent) {
    Layout::render(parent);
    if(index==uint(-1)) {  fill(parent+position+Rect(size), 128); return; } //no active tab
    Widget& current = at(index);
    if(index>0) fill(parent+position+Rect(int2(current.position.x, size.y)), 128); //dark inactive tabs before current
    fill(parent+position+current.position+Rect(int2(current.size.x,size.y)), 240); //light active tab
    if(index<count()-1) fill(parent+position+Rect(int2(current.position.x+current.size.x, 0), size), 128); //dark inactive tabs after current
}

/// ImageView
int2 ImageView::sizeHint() { return int2(image.width,image.height); }
void ImageView::render(int2 parent) {
    if(!image) return;
    blit(parent+position+(Widget::size-image.size())/2, image); //TODO: alpha
}

/// TriggerKey

bool TriggerKey::mouseEvent(int2, Event event, Key) {
    if(event==Press) { triggered(); return true; }
    return false;
}

///  ToggleKey

ToggleKey::ToggleKey(const Image<byte4>& enableIcon, const Image<byte4>& disableIcon) : enableIcon(enableIcon), disableIcon(disableIcon) {}
int2 ToggleKey::sizeHint() { return int2(size,size); }
void ToggleKey::render(int2 parent) {
    if(!(enabled?disableIcon:enableIcon)) return;
    int size = min(Widget::size.x,Widget::size.y);
    blit(parent+position+(Widget::size-int2(size,size))/2, enabled?disableIcon:enableIcon);
}
bool ToggleKey::mouseEvent(int2, Event event, Key button) {
    if(event==Press && button==Key::Left) { enabled = !enabled; toggled(enabled); return true; }
    return false;
}
