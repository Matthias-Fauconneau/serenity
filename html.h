#pragma once
#include "http.h"
#include "xml.h"
#include "interface.h"

/// Asynchronously load an image, sending a signal if the image was not cached
struct ImageLoader {
    ImageLoader(URL&& url, Image* target, function<void()>&& imageLoaded, int2 size=0, uint maximumAge=24*60);
    /// Reference to target to load (need to stay valid)
    Image* target;
    /// function to call on load if the image was not cached.
    function<void()> imageLoaded;
    /// Preferred size
    int2 size;
    void load(const URL&, Map&&);
};

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
};
