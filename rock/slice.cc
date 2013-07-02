/// \file slice.cc Displays volume as slices
#include "view.h"
#include "volume.h"
#include "volume-operation.h"
#include "window.h"
#include "display.h"

/// Displays volume as slices
class(SliceView, View), virtual Widget {
    bool view(const string& metadata, const string& name, const buffer<byte>& data) override {
        if(volumes) return false;
        Volume volume = toVolume(metadata, data);
        if(!inRange(1u,volume.sampleSize,4u)) return false;
        names << String(name);
        volumes << move(volume);
        return true;
    }
    string name() override { return names[currentIndex]; }
    bool mouseEvent(int2 cursor, int2 size, Event unused event, Button button) {
        if(button==WheelDown||button==WheelUp) {
            int nextIndex = clip<int>(0,currentIndex+(button==WheelUp?1:-1),volumes.size-1);
            if(nextIndex == currentIndex) return true;
            updateView();
            return true;
        }
        if(!button) return false;
        float z = clip(0.f, float(cursor.x)/(size.x-1), 1.f);
        if(sliceZ != z) { sliceZ = z; updateView(); }
        return true;
    }
    int2 sizeHint() {
        assert_(volumes);
        const Volume& volume = volumes[currentIndex];
        return (volume.sampleCount-2*volume.margin).xy();
    }
    void render(int2 position, int2 size) {
        const Volume& volume = volumes[currentIndex];
        Image image = slice(volume, sliceZ, true, true, true);
        //while(2*image.size()<=size) image=upsample(image);
        int2 centered = position+(size-image.size())/2;
        blit(centered, image);
    }
    array<String> names;
    array<Volume> volumes;
    int currentIndex=0;
    float sliceZ = 1./2; // Normalized z coordinate of the currently shown slice
    signal<> updateView;

    bool renderVolume = false;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
};

