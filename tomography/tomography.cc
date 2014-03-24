#include "volume.h"
#include "window.h"

//#include "synthetic.h" // Links synthetic.cc
Volume8 synthetic(int3 size);

//#include "project.h" // Links project.cc
void project(const Image& image, const Volume8& volume, vec2 angles);

struct View : Widget {
    const Volume8& volume;
    int2 lastPos = 0;
    vec2 rotation = vec2(0, -PI/3);
    View(const Volume8& volume) : volume(volume) {}
    bool mouseEvent(int2 cursor, int2 size, Event, Button button) {
        int2 delta = cursor-lastPos;
        lastPos = cursor;
        if(!button) return false;
        rotation += vec2(-2*PI*delta.x/size.x,2*PI*delta.y/size.y);
        rotation.y = clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
        return true;
    }
    void render(const Image& target) override {
        project(target, volume, rotation);
    }
};

struct Tomography {
    Volume volume = synthetic(128);
    View view {volume};
    Window window {&view, int2(512), "Tomography"_};
    Tomography() {
        window.actions[Escape] = []{ exit(); };
        window.background = Window::NoBackground;
        window.show();
    }
} tomography;
