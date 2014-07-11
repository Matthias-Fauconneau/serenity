#pragma once
#include "widget.h"
#include "map.h"

/// Associates a \a label to a set of 2D \a points
struct DataSet {
    String label;
    map<real,real> points;
};
inline bool operator <(const DataSet& a, const DataSet& b) { return a.points && b.points && a.points.values.last() > b.points.values.last(); }

/// Plots graphs of 2D point data sets
struct Plot : virtual Widget {
    enum LegendPosition { TopLeft, TopRight, BottomLeft, BottomRight };
    Plot(const string& title=""_, bool plotPoints=false, bool plotLines=true, LegendPosition legendPosition=TopRight)
        : title(title), plotPoints(plotPoints), plotLines(plotLines), legendPosition(legendPosition) {}
    map<real,real>& operator[](const string& label) {
        for(DataSet& dataSet: dataSets) if(dataSet.label==label) return dataSet.points;
        dataSets << DataSet{copy(String(label)), {}}; return dataSets.last().points;
    }
    int2 sizeHint() override;
    void render() override;

    String title, xLabel, yLabel;
    bool log[2] = {false, false};
    array<DataSet> dataSets;
    bool plotPoints, plotLines;
    LegendPosition legendPosition;
    vec2 min = 0, max = 0;
};
