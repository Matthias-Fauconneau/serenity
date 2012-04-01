#pragma once
#include "http.h"
#include "xml.h"
#include "interface.h"

/// Asynchronously load an image
struct ImageLoader {
    ImageLoader(const URL& url, Image* target, delegate<void> imageLoaded, int2 size=int2(0,0));
    /// Reference to target to load (need to stay valid)
    Image* target;
    /// Trigger when target was loaded
    delegate<void> imageLoaded;
    /// Preferred size
    int2 size;
    void load(const URL&, array<byte>&&);
};

/// Crude HTML layout engine
struct HTML : VBox {
    /// Signal content changes to trigger updates
    signal<> contentChanged;

    void load(const URL& url, array<byte>&& document);
    void clear();

    int2 sizeHint() override { int2 hint=VBox::sizeHint(); return int2(-max(1024,abs(hint.x)),hint.y); }
    void render(int2 parent) override;

private:
    void layout(const URL& url, const Element& e);
    void flushText();
    void flushImages();
private:
    string text;
    array<URL> images;
};
