#pragma once
#include "drag.h"

struct ViewWidget : Drag {
    vec2 angles = 0;
    ViewWidget() {
        valueChanged = [this](vec2 value) {
            angles = float(2*π)*value;
            angles.x = clamp<float>(-π/3, angles.x, π/3);
            angles.y = clamp<float>(-π/3, -angles.y, π/3);
        };
    }
};
