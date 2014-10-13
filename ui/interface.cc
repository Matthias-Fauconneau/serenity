#include "interface.h"

// ScrollArea

Graphics ScrollArea::graphics(int2 size) {
    int2 hint = abs(widget().sizeHint(size));
    int2 view (horizontal?max(hint.x,size.x):size.x,vertical?max(hint.y,size.y):size.y);
    Graphics graphics;
    if(view <= size) return widget().graphics(size);
    else graphics.append(widget().graphics(view), vec2(offset));
    if(scrollbar && size.y<view.y)
        graphics.fills.append( vec2(size.x-scrollBarWidth, -offset.y*size.y/view.y), vec2(size.x,(-offset.y+size.y)*size.y/view.y), 1./2, 1.f/2 );
    return graphics;
}

bool ScrollArea::mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) {
    int2 hint = abs(widget().sizeHint(size));
    if(event==Press) {
        focus = this;
        if((button==WheelDown || button==WheelUp) && size.y<hint.y) {
            offset.y += (button==WheelUp?1:-1) * 64;
            offset = min(int2(0,0), max(size-hint, offset));
            return true;
        }
        if(button==LeftButton) { dragStartCursor=cursor, dragStartDelta=offset; }
    }
    if(event==Motion && button==LeftButton && size.y<hint.y && scrollbar && dragStartCursor.x>size.x-scrollBarWidth) {
        offset.y = min(0, max(size.y-hint.y, dragStartDelta.y-(cursor.y-dragStartCursor.y)*hint.y/size.y));
        return true;
    }
    if(widget().mouseEvent(cursor-offset,max(hint,size),event,button,focus)) return true;
    if(event==Motion && button==LeftButton && size.y<hint.y) {
        offset = min(int2(0,0), max(size-hint, dragStartDelta+cursor-dragStartCursor));
        return true;
    }
    return false;
}

// Progress

int2 Progress::sizeHint(int2) { return int2(-height,height); }
Graphics Progress::graphics(int2 size) {
    Graphics graphics;
    warn(minimum <= value && value <= maximum, minimum, value, maximum);
    int x = size.x*uint(value-minimum)/uint(maximum-minimum);
    graphics.fills.append(vec2(0,1), vec2(x,size.y-1-1), lightBlue, 1.f);
    graphics.fills.append(vec2(x,1), vec2(size.x-x,size.y-1-1), gray, 1.f);
    return graphics;
}

// ImageView

int2 ImageView::sizeHint(int2 size) { return min(image.size, size.x && image.size.x ? image.size*size.x/image.size.x : image.size); }

Graphics ImageView::graphics(int2 size) {
    Graphics graphics;
    if(image) {
        int2 target = min(image.size*size.x/image.size.x, image.size*size.y/image.size.y);
        graphics.blits.append(vec2(max(vec2(0),vec2((size-target)/2))), vec2(target), share(image));
    }
    return graphics;
}

// Slider

bool Slider::mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*&) {
    if((event == Motion || event==Press) && button==LeftButton) {
        value = minimum+cursor.x*uint(maximum-minimum)/size.x;
        if(valueChanged) valueChanged(value);
        return true;
    }
    return false;
}

// ImageLink

bool ImageLink::mouseEvent(int2, int2, Event event, Button, Widget*&) {
    if(event==Press) { if(triggered) triggered(); return true; }
    return false;
}

//  ToggleButton
bool ToggleButton::mouseEvent(int2, int2, Event event, Button button, Widget*&) {
    if(event==Press && button==LeftButton) { enabled = !enabled; image = enabled?share(disableIcon):share(enableIcon); toggled(enabled); return true; }
    return false;
}

// WidgetToggle
bool WidgetToggle::mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) {
    focus = this;
    size_t previousIndex = index;
    //index = button && event != Release;
    if(button) index = !index;
    return widgets[index]->mouseEvent(cursor, size, event, button, focus) || previousIndex != index;
}
bool WidgetToggle::keyPress(Key key, Modifiers modifiers) {
    size_t previousIndex = index;
    //if(key == Space) index = 1;
    if(key == Space) index = !index;
    return widgets[index]->keyPress(key, modifiers) || previousIndex != index;
}
bool WidgetToggle::keyRelease(Key key, Modifiers modifiers) {
    size_t previousIndex = index;
    //if(key == Space) index = 0;
    return widgets[index]->keyRelease(key, modifiers) || previousIndex != index;
}
