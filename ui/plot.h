#pragma once
#include "widget.h"
#include "map.h"

struct Plot : virtual Widget {
    int2 sizeHint() override;
    void render(int2 position, int2 size) override;

    String title, xlabel, ylabel;
    bool log[2] = {false, false};
    array<String> legends;
    array<map<float,float>> dataSets;
    bool plotPoints = true, plotLines = false;
    enum { TopLeft, TopRight, BottomLeft, BottomRight } legendPosition = TopRight;
    vec2 min = 0, max = 0;
};

