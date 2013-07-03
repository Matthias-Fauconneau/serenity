#pragma once
#include "view.h"
#include "volume.h"
#include "widget.h"
#include "function.h"

/// Displays volume as slices
class(SliceView, View), virtual Widget {
    bool view(const string& metadata, const string& name, const buffer<byte>& data) override;
    string name() override;
    bool mouseEvent(int2 cursor, int2 size, Event unused event, Button button) override;
    int2 sizeHint() override;
    void render(int2 position, int2 size) override;
    array<String> names;
    array<Volume> volumes;
    int currentIndex=0;
    float sliceZ = 1./2; // Normalized z coordinate of the currently shown slice
    signal<> updateView;

    bool renderVolume = false;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
};
