#pragma once
#include "view.h"
#include "volume.h"
#include "widget.h"

/// Displays volume as slices
struct SliceView : View {
    bool view(const string& metadata, const string& name, const buffer<byte>& data) override;
    string name() override;
    bool mouseEvent(int2 cursor, int2 size, Event unused event, Button button) override;
    int2 sizeHint() override;
    void render(const Image& target) override;
    array<String> names;
    array<Volume> volumes;
    int currentIndex=0;
    static float sliceZ; // Normalized z coordinate of the currently shown slice (static: synchronize Z coordinates across all slice views)

    bool renderVolume = false;
    int2 lastPos; float lastZ;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
};
template struct Interface<View>::Factory<SliceView>;
