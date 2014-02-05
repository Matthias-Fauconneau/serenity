#pragma once
#include "widget.h"
#include "map.h"

struct Plot : virtual Widget {
    enum LegendPosition { TopLeft, TopRight, BottomLeft, BottomRight };
    Plot(const string& title, ref<string>&& legends, bool plotLines=false, LegendPosition legendPosition=TopRight)
        : title(title), legends(apply(legends,[](string s){return String(s);})), plotPoints(!plotLines), plotLines(plotLines), legendPosition(legendPosition)
    { dataSets.grow(this->legends.size); }
    int2 sizeHint() override;
    void render(int2 position, int2 size) override;

    String title, xlabel, ylabel;
    bool log[2] = {false, false};
    array<String> legends;
    array<map<real,real>> dataSets;
    bool plotPoints = true, plotLines = false;
    LegendPosition legendPosition;
    vec2 min = 0, max = 0;
};
