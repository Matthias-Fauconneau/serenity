#pragma once
#include "http.h"
#include "xml.h"
#include "interface.h"

/// Crude HTML layout engine
struct HTML : VBox {
    void load(const URL& url);
    void clear();

    void render(int2 parent) override;

private:
    void layout(const URL& url, const Element& e);
    void flushText();
    void flushImages();
private:
    string text;
    array<Image> images;
};
