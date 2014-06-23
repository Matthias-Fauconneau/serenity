#include "project.h"
#include "window.h"

struct TamDanielson : Widget {
    const int3 volumeSize = int3(512);
    const int3 projectionSize = int3(512);
    Window window {this, "Tam Danielson Window"_, projectionSize.xy()};
    uint viewIndex = 0;
    void render() override {
        target.buffer.clear(byte4(0,0,0,0xFF));
        for(uint viewIndex: range(projectionSize.z)) {
            for(uint index: range(projectionSize.z)) {
                if(index == viewIndex) continue;
                const float2 imageCenter = float2(projectionSize.xy()-int2(1))/2.f;
                const bool doubleHelix = true;
                const float numberOfRotations = 1;
                const float4 view = Projection(volumeSize, projectionSize, doubleHelix, numberOfRotations).worldToView(viewIndex) * Projection(volumeSize, projectionSize, doubleHelix, numberOfRotations).imageToWorld(index)[3];
                float2 image = view.xy() / view.z + imageCenter; // Perspective divide + Image coordinates offset
                int2 integer = int2(round(image));
                if(integer>=int2(0) && integer<target.size()) target(integer.x, integer.y) = 0xFF;
            }
        }
    }
    bool mouseEvent(int2 cursor, int2 size, Event, Button button) {
        if(button) { viewIndex = clip(0, int(cursor.x*(projectionSize.z-1)/(size.x-1)), int(projectionSize.z-1)); render(); putImage(target); return true; }
        return false;
    }
} app;
