#pragma once
#include "widget.h"

struct Drag : virtual Widget {
    struct {
        vec2 cursor;
        vec2 value;
    } dragStart {0_, 0_};

    vec2 value = vec2(0, 0);

    Drag(vec2 value) : value(value) {}

    virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
        if(event == Press) dragStart = {cursor, value};
        if(event==Motion && button==LeftButton) {
            vec2 newValue = drag(dragStart.value, (cursor - dragStart.cursor) / size);
            if(newValue != value) { value = newValue; return true; }
            return false;
        }
        return false;
    }

    virtual vec2 drag(vec2 dragStartValue, vec2 normalizedDragOffset) { return dragStartValue + normalizedDragOffset; }
};
