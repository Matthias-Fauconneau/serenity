#pragma once
#include "http.h"
#include "xml.h"
#include "interface.h"

/// Asynchronously load an image
struct ImageLoader {
    ImageLoader(const URL& url, Image<rgb>* target, signal<>&& imageLoaded, int2 size=int2(0,0), uint maximumAge=24*60);
    /// Reference to target to load (need to stay valid)
    Image<rgb>* target;
    /// Trigger when target was loaded
    signal<> imageLoaded;
    /// Preferred size
    int2 size;
    void load(const URL&, array<byte>&&);
};

/// Crude HTML layout engine
struct HTML : VBox {
    URL url;
    /// Signal content changes to trigger updates
    signal<> contentChanged;

    /// Replace layout with content downloaded from \a link
    void go(const ref<byte>& link);
    /// Replace layout with existing \a document
    void load(const URL& url, array<byte>&& document);
    /// Append existing \a document to layout
    void append(const URL& url, array<byte>&& document);
    /// Clear any content
    void clear();

    int2 sizeHint() override { int2 hint=VBox::sizeHint(); return int2(hint.x,hint.y); }

    void layout(const URL& url, const Element& e);
    void flushText();
    void flushImages();

    string text;
    array<URL> images;
};
