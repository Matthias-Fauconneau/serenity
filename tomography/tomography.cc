#include "volume.h"
#include "window.h"

//#include "synthetic.h" // Links synthetic.cc
VolumeF synthetic(int3 size);

//#include "project.h" // Links project.cc
void project(const Imagef& target, const VolumeF& source, vec2 angles);
void projectTrilinear(const Imagef& target, const VolumeF& source, vec2 angles);
void update(const VolumeF& target, const Imagef& source, vec2 angles);
void updateMART(const VolumeF& target, const Imagef& source, vec2 angles);

struct View : Widget {
    const VolumeF* volume;
    int2 lastPos = 0;
    vec2 rotation = vec2(0, -PI/3);
    bool trilinear = false;

    View(const VolumeF* volume) : volume(volume) {}
    bool mouseEvent(int2 cursor, int2 size, Event, Button button) {
        int2 delta = cursor-lastPos;
        lastPos = cursor;
        if(!button) return false;
        rotation += vec2(-2*PI*delta.x/size.x,2*PI*delta.y/size.y);
        rotation.y = clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
        return true;
    }
    void render(const Image& target) override {
        Imagef linear {target.size()};
        if(trilinear) projectTrilinear(linear, *volume, rotation);
        else project(linear, *volume, rotation);
        convert(target, linear, norm(volume->sampleCount));
    }
};

struct Tomography {
    const int N = 256;
    VolumeF source = synthetic(N);
    VolumeF target {N};
    View view {&target};
    Window window {&view, int2(768), "Tomography"_};
    static constexpr bool MART = false;
    Tomography() {
        window.actions[Escape] = []{ exit(); };
        window.background = Window::NoBackground;
        window.actions['T'] = [this] { view.trilinear = !view.trilinear; };
        window.actions[Space] = [this] { if(view.volume == &source) view.volume = &target; else view.volume = &source; window.render(); };
        window.show();
        window.frameSent = {this, &Tomography::step};
        if(MART) target.data.clear(1);
    }
    Random random;
    void step() {
        for(int unused i: range(1)) {
            vec2 angles (2*PI*random(), -PI/2);
            Imagef sourceImage {source.sampleCount.xy()}; // Reconstructs at sensor resolution
            project(sourceImage, source, angles); // Projects validation
            Imagef targetImage {sourceImage.size()};
            project(targetImage, target, angles); // Projects reconstruction
            if(!MART) for(uint j: range(targetImage.data.size)) targetImage.data[j] = sourceImage.data[j] - targetImage.data[j]; // Algebraic
            else for(uint j: range(targetImage.data.size)) targetImage.data[j] = targetImage.data[j] ? sourceImage.data[j] / targetImage.data[j] : 0; // MART
            if(!MART) update(target, targetImage, angles);
            else updateMART(target, targetImage, angles); // Backprojects error image
        }
        if(view.volume == &target) window.render();
    }
} tomography;
