#pragma once
#include "drag.h"
#include "function.h"

struct ViewControl : Drag {
    //function<void(vec2)> valueChanged;
    vec2 angles;

    //ViewControl(function<void(vec2)> valueChanged) : valueChanged(valueChanged) {
    ViewControl() {
        Drag::valueChanged = [this](vec2 value) {
            angles = float(2*PI)*value;
            angles.x = clamp<float>(-PI/3, angles.x, PI/3);
            angles.y = clamp<float>(-PI/3, angles.y, PI/3);
            //this->valueChanged(angles);
        };
    }
};
