#pragma once
#include "widget.h"
#include "map.h"

struct Plot : virtual Widget {
    enum LegendPosition { TopLeft, TopRight, BottomLeft, BottomRight };
    Plot(const string& title=""_, bool plotPoints=true, bool plotLines=true, LegendPosition legendPosition=TopRight)
        : title(title), plotPoints(plotPoints), plotLines(plotLines), legendPosition(legendPosition) {}
    map<real,real>& operator[](const string& name) { return dataSets[name]; }
    int2 sizeHint() override;
    void render(const Image& target) override;

    String title, xlabel, ylabel;
    bool log[2] = {true, false};
    map<String, map<real,real>> dataSets;
    bool plotPoints = true, plotLines = false;
    LegendPosition legendPosition;
    vec2 min = 0, max = 0;
};
