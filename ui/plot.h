#pragma once
#include "widget.h"
#include "map.h"

struct Plot : virtual Widget {
    enum LegendPosition { TopLeft, TopRight, BottomLeft, BottomRight };
    Plot(string title=""_, bool plotLines=true, LegendPosition legendPosition=TopRight)
		: name(copyRef(title)), plotPoints(!plotLines), plotLines(plotLines), legendPosition(legendPosition) {}
	String title() override { return copyRef(name); }
 vec2f sizeHint(vec2f) override;
 shared<Graphics> graphics(vec2f size) override;

	String name, xlabel, ylabel;
 bool log[2] = {false, false};
 map<String, map<float,float>> dataSets;
 bool plotPoints = false, plotLines = true;
 LegendPosition legendPosition;
 vec2f min = 0, max = 0;
};
