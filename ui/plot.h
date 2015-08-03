#pragma once
#include "widget.h"
#include "map.h"

struct Plot : virtual Widget {
    enum LegendPosition { TopLeft, TopRight, BottomLeft, BottomRight };
    Plot(string title=""_, bool plotLines=false, LegendPosition legendPosition=TopRight)
		: name(copyRef(title)), plotPoints(!plotLines), plotLines(plotLines), legendPosition(legendPosition) {}
	String title() override { return copyRef(name); }
 vec2 sizeHint(vec2) override;
 shared<Graphics> graphics(vec2 size) override;

	String name, xlabel, ylabel;
 bool log[2] = {false, false};
 map<String, map<float,float>> dataSets;
 bool plotPoints = false, plotLines = true, plotBands = true;
 LegendPosition legendPosition;
 vec2 min = 0, max = 0;
};
