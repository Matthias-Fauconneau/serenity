#include "interface.h"
#include "display.h"
#include "layout.h"
#include "text.h"

// renderToImage
Image renderToImage(Widget& widget, int2 size) {
    Image framebuffer = move(::framebuffer);
    array<Rect> clipStack = move(::clipStack);
    Rect currentClip = move(::currentClip);
    ::framebuffer = Image(size.x, size.y);
    ::currentClip = Rect(::framebuffer.size());
    fill(Rect(::framebuffer.size()),1);
    assert(&widget);
    widget.render(0,::framebuffer.size());
    Image image = move(::framebuffer);
    ::framebuffer = move(framebuffer);
    ::clipStack = move(clipStack);
    ::currentClip = move(currentClip);
    return image;
}
// Provides weak symbols in case an application links interface only to render to image
__attribute((weak)) void setFocus(Widget*) { error("Window support not linked"_); }
 __attribute((weak)) bool hasFocus(Widget*) { error("Window support not linked"_); }
 __attribute((weak)) void setDrag(Widget*) { error("Window support not linked"_); }
 __attribute((weak)) String getSelection(bool) { error("Window support not linked"_); }
__attribute((weak)) void setCursor(Rect, Cursor) { error("Window support not linked"_); }

// ScrollArea
void ScrollArea::render(int2 position, int2 size) {
    this->size=size;
    int2 hint = abs(widget().sizeHint());
    widget().render(position+delta, int2(horizontal?max(hint.x,size.x):size.x,vertical?max(hint.y,size.y):size.y));
    if(scrollbar && size.y<hint.y) fill(size.x-scrollBarWidth, -delta.y*size.y/hint.y,size.x,(-delta.y+size.y)*size.y/hint.y, vec4(0.5));
}
bool ScrollArea::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    int2 hint = abs(widget().sizeHint());
    if(event==Press && (button==WheelDown || button==WheelUp) && size.y<hint.y) {
        delta.y += (button==WheelUp?1:-1) * 64;
        delta = min(int2(0,0), max(size-hint, delta));
        setFocus(this);
        return true;
    }
    if(event==Press && button==LeftButton) { dragStartCursor=cursor, dragStartDelta=delta; setFocus(this); }
    if(event==Motion && button==LeftButton && size.y<hint.y && scrollbar && dragStartCursor.x>size.x-scrollBarWidth) {
        delta.y = min(0, max(size.y-hint.y, dragStartDelta.y-(cursor.y-dragStartCursor.y)*hint.y/size.y));
        return true;
    }
    if(widget().mouseEvent(cursor-delta,max(hint,size),event,button)) return true;
    if(event==Motion && button==LeftButton && size.y<hint.y) {
        setDrag(this);
        delta = min(int2(0,0), max(size-hint, dragStartDelta+cursor-dragStartCursor));
        return true;
    }
    return false;
}
bool ScrollArea::keyPress(Key key, Modifiers) {
    int2 hint = abs(widget().sizeHint());
    if(key==PageUp || key==PageDown) { delta.y += (key==PageUp?1:-1) * size.y; delta = min(int2(0,0), max(size-hint, delta)); return true; }
    return false;
}
void ScrollArea::ensureVisible(Rect target) { delta = max(-target.min, min(size-target.max, delta)); }
void ScrollArea::center(int2 target) { delta = size/2-target; }

// Progress
int2 Progress::sizeHint() { return int2(-height,height); }
void Progress::render(int2 position, int2 size) {
    if(maximum > minimum && value >= minimum && value <= maximum) {
        int x = size.x*uint(value-minimum)/uint(maximum-minimum);
        fill(position+Rect(int2(0,1), int2(x,size.y-2)), highlight);
        fill(position+Rect(int2(x,1), int2(size.x,size.y-2)), darkGray);
    } else {
        fill(position+Rect(0, size), darkGray);
    }
}

// Slider
bool Slider::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    if((event == Motion || event==Press) && button==LeftButton) {
        value = minimum+cursor.x*uint(maximum-minimum)/size.x;
        valueChanged(value);
        return true;
    }
    return false;
}

// Selection
bool Selection::mouseEvent(int2 cursor, int2 unused size, Event event, Button button) {
    array<Rect> widgets = layout(0, size);
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
    index=i; if(index!=uint(-1)) activeChanged(index);
}

// HighlightSelection
void HighlightSelection::render(int2 position, int2 size) {
    array<Rect> widgets = layout(position, size);
    for(uint i: range(count())) {
        if(i==index && (always || hasFocus(this) || hasFocus(&at(i)))) fill(widgets[i], highlight);
        at(i).render(widgets[i]);
    }
}

// TabSelection
void TabSelection::render(int2 position, int2 size) {
    array<Rect> widgets = layout(position, size);
    if(index>=count()) fill(position+Rect(size), darkGray); //no active tab
    else {
        Rect active = widgets[index];
        if(index>0) fill(Rect(position, int2(active.min.x, position.y+size.y)), darkGray); //darken inactive tabs before current
        fill(active, lightGray); //light active tab
        if(index<count()-1) fill(Rect(int2(active.max.x,position.y), position+size), darkGray); //darken inactive tabs after current
    }
    for(uint i: range(count()))  at(i).render(widgets[i]);
}

// ImageWidget
int2 ImageWidget::sizeHint() { return image.size(); }
void ImageWidget::render(int2 position, int2 size) {
    if(!image) return;
    int2 pos = position+(size-image.size())/2;
    blit(pos, image);
}

// ImageLink
bool ImageLink::mouseEvent(int2, int2, Event event, Button) {
    if(event==Press) { triggered(); linkActivated(link); return true; }
    return false;
}

//  ToggleButton
int2 ToggleButton::sizeHint() { return (enabled?disableIcon:enableIcon).size(); }
void ToggleButton::render(int2 position, int2 size) {
    const Image& image = enabled?disableIcon:enableIcon;
    if(!image) return;
    int2 pos = position+(size-image.size())/2;
    blit(pos, image);
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
