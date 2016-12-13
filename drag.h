#pragma once
#include "widget.h"
#include "function.h"

struct Drag : virtual Widget {
    vec2 value = vec2(0, 0);
    function<void(vec2)> valueChanged;

    struct {
        vec2 cursor;
        vec2 value;
    } dragStart {0, 0};

    // Orbital ("turntable") view control
    virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
        if(event == Press) {
            dragStart = {cursor, value};
        }
        if(event==Motion && button==LeftButton) {
            value = dragStart.value + (cursor - dragStart.cursor) / size;
            valueChanged(value);
            return true;
        }
        return false;
    }
};
