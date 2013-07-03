#pragma once
#include "widget.h"
#include "sample.h"
#include "view.h"

struct Plot : virtual Widget {
    void render(int2 position, int2 size) override;

    String title, xlabel, ylabel;
    bool logx, logy;
    array<String> legends;
    array<NonUniformSample> dataSets;
};

class(PlotView, View), virtual Plot {
    bool view(const string& metadata, const string& name, const buffer<byte>& data) override;
    string name() override;
};
