#include "volume.h"
#include "window.h"

//#include "synthetic.h" // Links synthetic.cc
Volume8 synthetic(int3 size);

//#include "project.h" // Links project.cc
void project(const Image& image, const Volume8& volume, real angle);

struct View : Widget {
    const Volume8& volume;
    real angle = 0;
    View(const Volume8& volume) : volume(volume) {}
    bool mouseEvent(int2 cursor, int2 size, Event, Button button) {
        if(!button) return false;
        angle = 2*PI*cursor.x/size.x;
        return true;
    }
    void render(const Image& target) override {
        project(target, volume, angle);
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
