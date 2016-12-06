#pragma once
#include "drag.h"
#include "function.h"

struct ViewWidget : Drag {
    uint2 size;
    function<Image(uint2,vec2)> render;
    vec2 angles;
    Image image;
    ViewWidget(uint2 size, function<Image(uint2,vec2)> render) : size(size), render(render) {
        setValue = [this](vec2 value) {
            angles = float(2*PI)*value;
            angles.x = clamp<float>(-PI/3, angles.x, PI/3);
            angles.y = clamp<float>(-PI/3, angles.y, PI/3);
        };
    }
    virtual vec2 sizeHint(vec2) override { return vec2(size); }
    virtual shared<Graphics> graphics(vec2) override {
        image = render(uint2(size), angles);
        shared<Graphics> graphics;
        graphics->blits.append(vec2(0), vec2(image.size), unsafeShare(image));
        return graphics;
    }
};
