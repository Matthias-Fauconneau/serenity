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
            ImageF linear = phantom->project(target.size(), projection.scale(vec3(volume->sampleCount/2)));
            convert(target, linear/*, 2*/);
        } else {
            ImageF linear {target.size()};
            project(linear, *volume, projection);
            convert(target, linear/*, volume->sampleCount.x*/); //norm(volume->sampleCount)/norm(vec3(1)));
        }
    }
};

struct Tomography {
    const uint N = 512;
    Phantom phantom {N};
    VolumeF source = phantom.volume(N);
    map<Projection, ImageF> projections {N};
    VolumeF target {N};
    View view {&phantom, &source};
    Window window {&view, int2(704), "Tomography"_};
    Tomography() {
        window.actions[Escape] = []{ exit(); };
        window.background = Window::NoBackground;
        window.actions['p'] = [this] { view.phantom = view.phantom ? 0 : &phantom; window.render(); };
        window.actions[Space] = [this] { step(); };
        window.actions[Return] = [this] {
            if(view.volume == &source) view.volume = &target; else view.volume = &source;
            window.render();
        };
        for(string argument: arguments()) {
            if(argument=="view"_) {
                //view.phantom = 0;
                view.volume = &source;
            }
            if(argument=="compute"_) {
                for(uint i: range(1)) {
                    int3 size = target.sampleCount;
                    mat4 projection = mat4().rotateX(-PI/2 /*Pitch*/).rotateZ(2*PI*i/N /*Yaw*/).scale(1/max(max(size.x,size.y),size.z));
                    projections.insertMulti(projection, phantom.project(N, projection)); // Projects phantom
                }
                step();
                //window.frameSent = {this, &Tomography::step};
                view.phantom = 0;
                view.volume = &target;
            }
        }
        window.show();
    }
    Random random;
    void step() {
#if SART
        for(int unused i: range(1)) {
            mat4 projection = mat4().rotateX(-PI/2 /*Pitch*/).rotateZ(2*PI*random() /*Yaw*/);
            ImageF sourceImage = phantom.project(N, projection); // Projects phantom
            ImageF targetImage {sourceImage.size()};
            project(targetImage, target, projection); // Projects reconstruction
            for(uint j: range(targetImage.data.size)) targetImage.data[j] = sourceImage.data[j] - targetImage.data[j]; // Algebraic
            updateSART(target, targetImage, projection);
        }
#else
        updateSIRT(target, projections);
#endif
        if(view.volume == &target) window.render();
    }
} tomography;
