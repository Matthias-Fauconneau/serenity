#pragma once
#include "widget.h"
#include "sample.h"

struct Plot : virtual Widget {
    int2 sizeHint() override;
    void render(int2 position, int2 size) override;

    String title, xlabel, ylabel;
    bool logx=false, logy=false;
    array<String> legends;
    array<NonUniformSample> dataSets;
};

