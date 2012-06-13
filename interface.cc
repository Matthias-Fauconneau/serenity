#include "interface.h"
#include "window.h"
#include "font.h"
#include "raster.h"

#include "layout.h"
template struct item<Icon>;
template struct item<Text>;
template struct item<Space>;
template struct tuple<Icon,Text,Space>;
template struct Tuple<Icon,Text,Space>;

#include "array.cc"
template struct array<ImageView>;

Space space;

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
        int x = size.x*(value-minimum)/(maximum-minimum);
        fill(parent+position+Rect(int2(0,0), int2(x,size.y)), gray(128));
        fill(parent+position+Rect(int2(x,0), size), gray(192));
    } else {
        fill(parent+position+Rect(int2(0,0), size), gray(128));
    }
}

bool Slider::mouseEvent(int2 position, Event event, Button button) {
    if((event == Motion || event==Press) && button==LeftButton) {
        value = minimum+position.x*(maximum-minimum)/size.x;
        valueChanged.emit(value);
        return true;
    }
    return false;
}

/// Selection

bool Selection::mouseEvent(int2 position, Event event, Button button) {
    if(Layout::mouseEvent(position,event,button)) return true;
    if(event != Press) return false;
    if(button == WheelDown && index>0 && index<count()) { index--; at(index).selectEvent(); activeChanged.emit(index); return true; }
    if(button == WheelUp && index<count()-1) { index++; at(index).selectEvent(); activeChanged.emit(index); return true; }
    Window::focus=this;
    for(uint i=0;i<count();i++) { Widget& child=at(i);
        if(position>=child.position && position<child.position+child.size) {
            if(index!=i) { index=i; at(index).selectEvent(); activeChanged.emit(index); }
            if(button == LeftButton) itemPressed.emit(index);
            return true;
        }
    }
    return false;
}

bool Selection::keyPress(Key key) {
    if(key==Down && index<count()-1) { index++; at(index).selectEvent(); activeChanged.emit(index); return true; }
    if(key==Up && index>0 && index<count()) { index--; at(index).selectEvent(); activeChanged.emit(index); return true; }
    return false;
}

void Selection::setActive(uint i) {
    assert(i==uint(-1) || i<count());
    if(index!=i) { index=i; if(index!=uint(-1)) { at(index).selectEvent(); activeChanged.emit(index); } }
}

/// HighlightSelection

void HighlightSelection::render(int2 parent) {
    if(index<count()) {
        Widget& current = at(index);
        if(position+current.position>=int2(-4,-4) && current.position+current.size<=(size+int2(4,4))) {
            fill(parent+position+current.position+Rect(current.size), byte4(224, 192, 128, 224));
        }
    }
    Layout::render(parent);
}

/// TabSelection

void TabSelection::render(int2 parent) {
    Layout::render(parent);
    if(index>=count()) {
        //darken whole tabbar
        fill(parent+position+Rect(size), gray(224), Multiply);
        return;
    }
    Widget& current = at(index);

    //darken inactive tabs
    fill(parent+position+Rect(int2(current.position.x, size.y)), gray(224), Multiply);
    if(current.position.x+current.size.x<size.x-1-int(count())) //dont darken only margin
        fill(parent+position+Rect( int2(current.position.x+current.size.x, 0), size ), gray(224), Multiply);
}

/// ImageView

int2 ImageView::sizeHint() { return int2(image.width,image.height); }
void ImageView::render(int2 parent) {
    if(!image) return;
    blit(parent+position+(Widget::size-image.size())/2, image, Alpha);
}

/// TriggerButton

bool TriggerButton::mouseEvent(int2, Event event, Button) {
    if(event==Press) { triggered.emit(); return true; }
    return false;
}

///  ToggleButton

ToggleButton::ToggleButton(const Image& enableIcon, const Image& disableIcon) : enableIcon(enableIcon), disableIcon(disableIcon) {}
int2 ToggleButton::sizeHint() { return int2(size,size); }
void ToggleButton::render(int2 parent) {
    if(!(enabled?disableIcon:enableIcon)) return;
    int size = min(Widget::size.x,Widget::size.y);
    blit(parent+position+(Widget::size-int2(size,size))/2, enabled?disableIcon:enableIcon, Alpha);
}
bool ToggleButton::mouseEvent(int2, Event event, Button button) {
    if(event==Press && button==LeftButton) { enabled = !enabled; toggled.emit(enabled); return true; }
    return false;
}
