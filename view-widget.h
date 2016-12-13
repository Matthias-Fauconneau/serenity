#pragma once
#include "drag.h"
#include "function.h"

struct ViewWidget : Drag {
    uint2 size;
    function<void(vec2)> valueChanged;
    vec2 angles;

    ViewWidget(uint2 size, function<void(vec2)> valueChanged) : size(size), valueChanged(valueChanged) {
        Drag::valueChanged = [this](vec2 value) {
            angles = float(2*PI)*value;
            angles.x = clamp<float>(-PI/3, angles.x, PI/3);
            angles.y = clamp<float>(-PI/3, angles.y, PI/3);
            this->valueChanged(angles);
        };
    }
    virtual vec2 sizeHint(vec2) override { return vec2(size); }
    virtual shared<Graphics> graphics(vec2) override { return shared<Graphics>(); }
};
