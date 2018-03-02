#pragma once
#include "widget.h"
#include "function.h"

struct Drag : virtual Widget {
    function<void()> onDragStart;
    function<void(vec2)> valueChanged;

    struct {
        vec2 cursor;
        vec2 value;
    } dragStart {0, 0};

    vec2 value = vec2(0, 0);

    virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
        if(event == Press) {
            dragStart = {cursor, value};
            if(onDragStart) onDragStart();
        }
        if(event==Motion && button==LeftButton) {
            value = dragStart.value + (cursor - dragStart.cursor) / size;
            if(valueChanged) valueChanged(value);
            return true;
        }
        return false;
    }
};
