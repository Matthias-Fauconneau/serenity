#include "thread.h"
#include "cdf.h"
#include "view.h"
#include "project.h"
#include "layout.h"
#include "window.h"

struct Viewer : HBox {
    buffer<VolumeF> volumes;
    uint currentIndex = 0;
    const VolumeF* current() { return &volumes[currentIndex]; }
    buffer<Projection> projections = evaluateProjections(current()->size, int2(256,256), 256, 1, true); //evaluateProjections(current()->sampleCount, int2(504,378), 5041, 1, false);
    SliceView sliceView {current(), 2};
    VolumeView volumeView {current(), projections,  int2(256,256), 2};
    Window window;

    Viewer(ref<string> paths) :
        HBox{{ &sliceView , &volumeView }},
        volumes ( apply(paths, [&](string path){ return VolumeF(parseSize(path), cast<float>((ref<byte>)Map(path))); }) ),
        window (this, join(apply(paths,[](string path){ return section(section(path,'/',-2,-1),'.'); })," "_)) {
    }
    bool mouseEvent(const Image& target, int2 cursor, int2 size, Event event, Button button) override {
        if((button == WheelUp &&  currentIndex> 0) || (button == WheelDown && currentIndex < volumes.size-1)) {
            if(button == WheelUp) currentIndex--; else currentIndex++;
            sliceView.volume = current();
            volumeView.volume = current();
            return true;
        }
        return HBox::mouseEvent(target, cursor, size, event, button);
    }
} app ( arguments() );
