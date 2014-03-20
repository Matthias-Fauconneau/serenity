#pragma once
#include "volume.h"
#include "widget.h"

/// Displays a volume slice
#if INTERFACE
#include "view.h"
struct SliceView : View {
    bool view(const string& metadata, const string& name, const buffer<byte>& data) override;
    string name() override;
#else
struct SliceView : Widget {
#endif
    bool mouseEvent(int2 cursor, int2 size, Event unused event, Button button) override;
    int2 sizeHint() override;
    void render(const Image& target) override;

    bool view(Volume&& volume, const string& name);
    SliceView(Volume&& volume, const string& name=""_) { view(move(volume), name); }

    array<String> names;
    array<Volume> volumes;
    int currentIndex=0;
    static float sliceZ; // Normalized z coordinate of the currently shown slice (static: synchronize Z coordinates across all slice views)

    bool renderVolume = false;
    int2 lastPos; float lastZ;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
};
