#include "error.h"
#include "thread.h"
#include "cdf.h"
#include "project.h"
#include "view.h"
#include "window.h"

struct Evaluation : Widget {
    VolumeCDF projectionData;
    buffer<Map> files;
    buffer<VolumeF> volumes;
    const uint stride = 4;
    buffer<ImageF> images = sliceProjectionVolume(projectionData.volume, stride);
    buffer<Projection> projections = evaluateProjections(volumes.first().sampleCount, images[0].size(), images.size, stride);
    ProjectionView projectionView {images, 2};
    VolumeView volumeView {&volumes.first(), projections, 2};
    uint currentIndex = 0;
    Widget* widget() { return currentIndex>0 ? (Widget*)&volumeView : &projectionView; }
    Window window;

    Evaluation(string projectionPath, ref<string> paths) :
        projectionData( Folder(projectionPath) ),
        files( apply(paths,[](string path){ return Map(path); } ) ),
        volumes ( apply(paths.size, [&](uint i){ return VolumeF(parseSize(paths[i]), cast<float>((ref<byte>)files[i])); }) ),
        window (this, join(apply(paths,[](string path){ return section(section(path,'/',-2,-1),'.'); })," "_)) {
        const float SSQ = ::SSQ(images);
        log(projectionPath, images.size, images.first().size());
        for(uint i: range(paths.size)) {
            const VolumeF& volume = volumes[i];
            const float SSE = ::SSE(volume,  projections, images);
            const float MSE = SSE / SSQ;
            const float PSNR = 10 * log10(MSE);
            log(paths[i], "MSE", percent(MSE), "PSNR", dec(-round(PSNR)));
        }
    }
    bool mouseEvent(const Image& target, int2 cursor, int2 size, Event event, Button button) override {
        if((button == WheelUp &&  currentIndex> 0) || (button == WheelDown && currentIndex < volumes.size-1)) {
            if(button == WheelUp) currentIndex--; else currentIndex++;
            if(currentIndex>0) volumeView.volume = &volumes[currentIndex-1];
            return true;
        }
        return widget()->mouseEvent(target, cursor, size, event, button);
    }
    void render(const Image& target) override { return widget()->render(target); }
} app ( arguments().first(), arguments().slice(1) );
