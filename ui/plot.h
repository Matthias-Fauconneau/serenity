#pragma once
#include "widget.h"
#include "map.h"

struct Plot : Widget {
    enum LegendPosition { TopLeft, TopRight, BottomLeft, BottomRight };
    Plot(const string& title=""_, bool plotLines=false, LegendPosition legendPosition=TopRight)
        : title(title), plotPoints(!plotLines), plotLines(plotLines), legendPosition(legendPosition) {}
    int2 sizeHint() override;
    void render(const Image& target) override;

    String title, xlabel, ylabel;
    bool log[2] = {false, false};
    map<String, map<real,real>> dataSets;
    bool plotPoints = true, plotLines = false;
    LegendPosition legendPosition;
    vec2 min = 0, max = 0;
};
