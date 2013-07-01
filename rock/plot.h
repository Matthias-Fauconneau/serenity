#pragma once
#include "widget.h"
#include "sample.h"

struct Plot : virtual Widget {
    void render(int2 position, int2 size) override;

    String title, xlabel, ylabel;
    bool logx, logy;
    array<String> legends;
    array<NonUniformSample> dataSets;
};
