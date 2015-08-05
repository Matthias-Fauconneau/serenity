#pragma once
/// \file html.h simple HTML layout engine
#include "http.h"
#include "xml.h"
#include "interface.h"

/// Simple HTML layout engine
struct HTML : VBox {
    URL url;
    /// Signal content changes to trigger updates
    signal<> contentChanged;

    /// Displays document at \a link
    void go(const ref<byte>& link);
    /// Displays \a document
    void load(const URL& url, Map&& document);

    int2 sizeHint() override { int2 hint=VBox::sizeHint(); return int2(hint.x,hint.y); }

    void parse(const URL& url, const Element& e);
    void flushText();
    void flushImages();

    string text;
    array<URL> images;
    int paragraphCount;
    array<string> linkStack;
    array<unique<Text>> texts; //weakly referenced by VBox
    array<unique<UniformGrid<ImageLink>>> grids; //weakly referenced by VBox
};
