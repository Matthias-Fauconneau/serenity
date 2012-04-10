#pragma once
#include "http.h"
#include "xml.h"
#include "interface.h"

/// Asynchronously load an image
struct ImageLoader {
    ImageLoader(const URL& url, Image* target, delegate<void()> imageLoaded, int2 size=int2(0,0), uint maximumAge=2*60*60);
    /// Reference to target to load (need to stay valid)
    Image* target;
    /// Trigger when target was loaded
    delegate<void()> imageLoaded;
    /// Preferred size
    int2 size;
    void load(const URL&, array<byte>&&);
};

/// Crude HTML layout engine
struct HTML : VBox {
    /// Signal content changes to trigger updates
    signal<> contentChanged;

    /// Download \a link and layout
    void go(const string& link);
    /// Layout existing \a document
    void load(const URL& url, array<byte>&& document);
    /// Clear any content
    void clear();

    int2 sizeHint() override { int2 hint=VBox::sizeHint(); return int2(-max(1024,abs(hint.x)),hint.y); }

    void layout(const URL& url, const Element& e);
    void flushText();
    void flushImages();
    void imageLoaded();

    string text;
    array<URL> images;
};
