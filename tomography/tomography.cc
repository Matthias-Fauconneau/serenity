#include "volume.h"
#include "window.h"

//#include "synthetic.h" // Links synthetic.cc
Volume8 synthetic(int3 size);

//#include "project.h" // Links project.cc
void project(const Imagef& target, const Volume8& source, vec2 angles);
void projectMean(const Imagef& target, const Volume8& source, vec2 angles);
void update(const Volume8& target, const Imagef& source, vec2 angles);

struct View : Widget {
    const Volume8* volume;
    int2 lastPos = 0;
    vec2 rotation = vec2(0, -PI/3);
    View(const Volume8* volume) : volume(volume) {}
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
        convert(target, linear, 0xFFF);
    }
};

struct Tomography {
    Volume8 source = synthetic(128);
    Volume8 target {128};
    View view {&target};
    Window window {&view, int2(512), "Tomography"_};
    Tomography() {
        window.actions[Escape] = []{ exit(); };
        window.background = Window::NoBackground;
        window.actions[Space] = [this] { if(view.volume == &source) view.volume = &target; else view.volume = &source; window.render(); };
        window.show();

        // Ordered single projection (uniform angle sampling)
        const uint projectionCount = 64;
        for(int i: range(projectionCount)) {
            vec2 angles (2*PI*i/projectionCount, -PI/2);
            Imagef sourceImage {source.sampleCount.xy()}; // Reconstructs at sensor resolution
            projectMean(sourceImage, source, angles); // Projects validation
            Imagef targetImage {sourceImage.size()};
            projectMean(targetImage, target, angles); // Projects reconstruction
            constexpr bool add = true;
            if(add) for(uint j: range(targetImage.data.size)) targetImage.data[j] = sourceImage.data[j] - targetImage.data[j]; // Algebraic
            else for(uint j: range(targetImage.data.size)) targetImage.data[j] = targetImage.data[j] ? sourceImage.data[j] / targetImage.data[j] : 0; // MART
            assert_(add);
            update(target, targetImage, angles); // Backprojects error image
        }
    }
} tomography;
