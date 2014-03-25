#include "volume.h"
#include "window.h"

//#include "synthetic.h" // Links synthetic.cc
VolumeF synthetic(int3 size);

//#include "project.h" // Links project.cc
void project(const Imagef& target, const VolumeF& source, vec2 angles);
//void projectMean(const Imagef& target, const VolumeF& source, vec2 angles);
void update(const VolumeF& target, const Imagef& source, vec2 angles);
void updateMART(const VolumeF& target, const Imagef& source, vec2 angles);

template<Type T> struct View : Widget {
    const VolumeT<T>* volume;
    int2 lastPos = 0;
    vec2 rotation = vec2(0, -PI/3);
    View(const VolumeT<T>* volume) : volume(volume) {}
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
        project(linear, *volume, rotation);
        convert(target, linear, /*0xFFFF**/norm(volume->sampleCount));
    }
};

struct Tomography {
    VolumeF source = synthetic(128);
    VolumeF target {128};
    View<float> view {&target};
    Window window {&view, int2(512), "Tomography"_};
    static constexpr bool MART = false;
    Tomography() {
        window.actions[Escape] = []{ exit(); };
        window.background = Window::NoBackground;
        window.actions[Space] = [this] { if(view.volume == &source) view.volume = &target; else view.volume = &source; window.render(); };
        window.show();
        window.frameSent = {this, &Tomography::step};
        if(MART) target.data.clear(1);
    }
    uint level = 1, index = 0;
    Random random;
    void step() {
        //vec2 angles (2*PI*index/level, -PI/2);
        vec2 angles (2*PI*random(), -PI/2);
        Imagef sourceImage {source.sampleCount.xy()}; // Reconstructs at sensor resolution
        project(sourceImage, source, angles); // Projects validation
        Imagef targetImage {sourceImage.size()};
        project(targetImage, target, angles); // Projects reconstruction
        if(!MART) for(uint j: range(targetImage.data.size)) targetImage.data[j] = sourceImage.data[j] - targetImage.data[j]; // Algebraic
        else for(uint j: range(targetImage.data.size)) targetImage.data[j] = targetImage.data[j] ? sourceImage.data[j] / targetImage.data[j] : 0; // MART
        if(!MART) update(target, targetImage, angles);
        else updateMART(target, targetImage, angles); // Backprojects error image
        index++;
        if(index == level) index=0, level*=2;
        if(view.volume == &target) window.render();
        //window.setTitle(str(view.rotation.x));
    }
} tomography;
