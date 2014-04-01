#include "volume.h"
#include "matrix.h"
#include "window.h"
#include "phantom.h"
#include "project.h"

struct View : Widget {
    const Phantom* phantom;
    const VolumeF* volume;
    int2 lastPos = 0;
    vec2 rotation = vec2(0, -PI/3);
    bool trilinear = false;

    View(const Phantom* phantom, const VolumeF* volume) : phantom(phantom), volume(volume) {}
    bool mouseEvent(int2 cursor, int2 size, Event, Button button) {
        int2 delta = cursor-lastPos;
        lastPos = cursor;
        if(!button) return false;
        rotation += vec2(-2*PI*delta.x/size.x,2*PI*delta.y/size.y);
        rotation.y = clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
        return true;
    }
    void render(const Image& target) override {
        mat4 projection = mat4().rotateX(rotation.y /*Pitch*/).rotateZ(rotation.x /*Yaw*/);
        if(phantom) {
            ImageF linear = phantom->project(target.size(), projection);
            convert(target, linear, 2);
        } else {
            ImageF linear {target.size()};
            if(trilinear) projectTrilinear(linear, *volume, projection);
            else project(linear, *volume, projection);
            convert(target, linear, volume->sampleCount.x); //norm(volume->sampleCount)/norm(vec3(1)));
        }
    }
};

struct Tomography {
    const uint N = 128;
    Phantom phantom {10};
    VolumeF source = phantom.volume(N);
    VolumeF target {N};
    View view {&phantom, &source};
    Window window {&view, int2(512), "Tomography"_};
    Tomography() {
        window.actions[Escape] = []{ exit(); };
        window.background = Window::NoBackground;
        window.actions['p'] = [this] { view.phantom = view.phantom ? 0 : &phantom; window.render(); };
        window.actions['t'] = [this] { view.trilinear = !view.trilinear; window.render(); };
        window.actions[Space] = [this] { step(); };
        window.actions[Return] = [this] {
            if(view.volume == &source) view.volume = &target; else view.volume = &source;
            window.render();
        };
        window.show();
        for(string argument: arguments()) {
            if(argument=="view"_) view.volume = &source;
            if(argument=="compute"_) {
                view.phantom = 0;
                view.volume = &target;
                window.frameSent = {this, &Tomography::step};
            }
        }
    }
    Random random;
    void step() {
        for(int unused i: range(1)) {
            mat4 projection = mat4().rotateX(-PI/2 /*Pitch*/).rotateZ(2*PI*random() /*Yaw*/);
            ImageF sourceImage = phantom.project(N, projection); // Projects phantom
            ImageF targetImage {sourceImage.size()};
            projectTrilinear(targetImage, target, projection); // Projects reconstruction
            for(uint j: range(targetImage.data.size)) targetImage.data[j] = sourceImage.data[j] - targetImage.data[j]; // Algebraic
            update(target, targetImage, projection);
        }
        if(view.volume == &target) window.render();
    }
} tomography;
