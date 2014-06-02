#include "thread.h"
#include "cdf.h"
#include "view.h"
#include "project.h"
#include "layout.h"
#include "window.h"
#include "error.h"

struct Viewer : HBox {
    buffer<VolumeF> volumes;
    uint currentIndex = 0;
    const VolumeF* current() { return &volumes[currentIndex]; }
    buffer<Projection> projections = evaluateProjections(current()->sampleCount, int2(256,256), 256); //evaluateProjections(current()->sampleCount, int2(504,378), 5041);
    SliceView sliceView {current(), 2};
    VolumeView volumeView {current(), projections, 2};
    Window window;

    Viewer(ref<string> paths) :
        HBox{{ &sliceView , &volumeView }},
        volumes ( apply(paths, [&](string path){ return VolumeF(parseSize(path), cast<float>((ref<byte>)Map(path))); }) ),
        window (this, join(apply(paths,[](string path){ return section(section(path,'/',-2,-1),'.'); })," "_)) {
        if(volumes.size == 2) {
            const float MSE = SSE(volumes[0], volumes[1])/sqrt(SSQ(volumes[0])*SSQ(volumes[1]));
            const float PSNR = 10 * log10(MSE);
            log(MSE, PSNR);
        }
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
