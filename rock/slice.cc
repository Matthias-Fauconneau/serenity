/// \file slice.cc Displays volume as slices
#include "view.h"
#include "volume.h"
#include "volume-operation.h"
#include "window.h"
#include "display.h"

class(SliceView, View), Widget {
    SliceView() {
        window.localShortcut(Escape).connect([]{exit();});
        window.clearBackground = false;
    }
    bool view(shared<Result>&& result) override {
        if(!inRange(1u,toVolume(result).sampleSize,4u)) return false;
        results << result;
        updateView();
        window.show();
        return true;
    }
    bool mouseEvent(int2 cursor, int2 size, Event unused event, Button button) {
        if(button==WheelDown||button==WheelUp) {
            int nextIndex = clip<int>(0,currentIndex+(button==WheelUp?1:-1),results.size-1);
            if(nextIndex == currentIndex) return true;
            updateView();
            return true;
        }
        if(!button) return false;
        float z = clip(0.f, float(cursor.x)/(size.x-1), 1.f);
        if(sliceZ != z) { sliceZ = z; updateView(); }
        updateView();
        return true;
    }
    void updateView() {
        Volume volume = toVolume(results[currentIndex]);
        int2 size = volume.sampleCount.xy();
        while(2*size<displaySize) size *= 2;
        if(window.size != size) window.setSize(size);
        else window.render();
        window.setTitle(str(results[currentIndex]));
    }
    void render(int2 position, int2 size) {
        assert(result);
        Volume volume = toVolume(results[currentIndex]);
        Image image = slice(volume, sliceZ/*, result->relevantArguments.contains("cylinder"_)*/);
        while(2*image.size()<=size) image=upsample(image);
        blit(position, image);
    }
    array<shared<Result>> results;
    int currentIndex=0;
    float sliceZ = 1./2; // Normalized z coordinate of the currently shown slice
    Window window{this, int2(-1,-1), "SliceView"_};

    bool renderVolume = false;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
};

