#pragma once
#include "widget.h"
#include "map.h"

struct DataSet {
    String label;
    map<real,real> data;
};
inline bool operator <(const DataSet& a, const DataSet& b) { return a.data && b.data && a.data.values.last() > b.data.values.last(); }

struct Plot : virtual Widget {
    enum LegendPosition { TopLeft, TopRight, BottomLeft, BottomRight };
    Plot(const string& title=""_, bool plotPoints=true, bool plotLines=true, LegendPosition legendPosition=TopRight)
        : title(title), plotPoints(plotPoints), plotLines(plotLines), legendPosition(legendPosition) {}
    map<real,real>& operator[](const string& label) {
        for(DataSet& dataSet: dataSets) if(dataSet.label==label) return dataSet.data;
        dataSets << DataSet{String(label), {}}; return dataSets.last().data;
    }
    int2 sizeHint() override;
    void render() override;

    String title, xlabel, ylabel;
    bool log[2] = {false, false};
    array<DataSet> dataSets;
    bool plotPoints = true, plotLines = false;
    LegendPosition legendPosition;
    vec2 min = 0, max = 0;
};
