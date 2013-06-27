/// \file slice.cc Displays volume as slices
#include "view.h"
#include "volume.h"
#include "volume-operation.h"
#include "window.h"
#include "display.h"

class(SliceView, View), Widget {
    bool view(shared<Result>&& result) override {
        if(!inRange(1u,toVolume(result).sampleSize,4u)) return false;
        this->result = move(result);
        window.setTitle(str(this->result));
        window.localShortcut(Escape).connect([]{exit();});
        window.clearBackground = false;
        updateView();
        window.show();
        return true;
    }
    bool mouseEvent(int2 cursor, int2 size, Event unused event, Button button) {
        if(!button) return false;
        float z = clip(0.f, float(cursor.x)/(size.x-1), 1.f);
        if(sliceZ != z) { sliceZ = z; updateView(); }
        updateView();
        return true;
    }
    void updateView() {
        assert(result);
        Volume volume = toVolume(result);
        int2 size = volume.sampleCount.xy();
        while(2*size<displaySize) size *= 2;
        if(window.size != size) window.setSize(size);
        else window.render();
    }
    void render(int2 position, int2 size) {
        assert(result);
        Volume volume = toVolume(result);
        if(volume.sampleSize==20) { exit(); return; } // Don't try to display ASCII
        if(volume.sampleSize>4) error(result->name, volume.sampleSize);
        Image image = slice(volume, sliceZ);
        while(2*image.size()<=size) image=upsample(image);
        blit(position, image);
    }
    shared<Result> result;
    float sliceZ = 1./2; // Normalized z coordinate of the currently shown slice
    Window window{this, int2(-1,-1), "SliceView"_};

    bool renderVolume = false;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
};

