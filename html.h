#pragma once
#include "http.h"
#include "xml.h"
#include "interface.h"

/// Crude HTML layout engine
struct HTML : VBox {
    /// URL shown by this widget
    URL url;
    /// Signal content changes to trigger updates
    signal<> contentChanged;

    void load(array<byte>&& document);
    void clear();

    int2 sizeHint() override { int2 hint=VBox::sizeHint(); return int2(-max(1024,abs(hint.x)),hint.y); }
    void render(int2 parent) override;

private:
    void layout(const Element& e);
    void flushText();
    void flushImages();
private:
    string text;
    array<URL> images;
};
