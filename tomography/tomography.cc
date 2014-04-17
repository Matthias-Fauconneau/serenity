#include "volume.h"
#include "matrix.h"
#include "window.h"
#include "phantom.h"
#include "project.h"
#include "layout.h"

struct View : Widget {
    const Phantom* phantom;
    const VolumeF* volume;
    int2 lastPos = 0;
    static vec2 rotation; // Shared between all views

    View(const Phantom* phantom, const VolumeF* volume) : phantom(phantom), volume(volume) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) {
        int2 delta = cursor-lastPos;
        lastPos = cursor;
        if(!button || event != Motion) return false;
        rotation += vec2(-2*PI*delta.x/size.x,2*PI*delta.y/size.y);
        rotation.y = clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
        return true;
    }
    void render(const Image& target) override {
        mat4 projection = mat4().rotateX(rotation.y /*Pitch*/).rotateZ(rotation.x /*Yaw*/).scale(norm(target.size())/norm(volume->sampleCount));
        float max = 0; //volume->sampleCount.x;
        if(phantom) {
            ImageF linear = float(volume->sampleCount.x/2) * phantom->project(target.size(), projection.scale(vec3(volume->sampleCount)/2.f));
            convert(target, linear, max);
        } else {
            ImageF linear {target.size()};
            project(linear, *volume, Projection(projection, target.size()));
            convert(target, linear, max);
        }
    }
};
vec2 View::rotation = vec2(PI/4, -PI/3);

struct Tomography {
    const uint N = 64;
    const uint P = N;
    Phantom phantom {16};
    VolumeF source = phantom.volume(N);
    buffer<Projection> projections {P};
    buffer<ImageF> images {P};
    //SIRT reconstruction{N};
    CGNR reconstruction{N};
#if 1
    UniformGrid<View> views{{{0, &reconstruction.AtAp}, {0, &reconstruction.r}, {0, &reconstruction.p}, {&phantom, &source}}};
#else
    UniformGrid<View> views{{{&phantom, &source}}};
#endif
    View& view = views.last();
    Window window {&views, int2(1024*3/2), "Tomography"_};
    Tomography() {
        window.actions[Escape] = []{ exit(); };
        window.background = Window::NoBackground;
        window.actions['p'] = [this] {
            if(view.phantom) view.phantom = 0, window.setTitle("Source");
            else view.phantom = &phantom, window.setTitle("Phantom");
            window.render();
        };
        window.actions[Space] = [this] { step(); };
        window.actions[Return] = [this] {
            if(view.volume == &source) view.volume = &reconstruction.x; else view.volume = &source;
            window.render();
        };
        for(string argument: arguments()) {
            if(argument=="view"_) {
                view.phantom = 0;
                view.volume = &source;
            }
            if(argument=="compute"_) {
                float sum = 0;
                for(uint i: range(projections.size)) { // Projects phantom
                    mat4 projection = mat4().rotateX(-PI/2 /*Pitch*/).rotateZ(2*PI*i/N /*Yaw*/);
                    ImageF image = float((N-1)/2) * phantom.project(N, projection.scale(vec3(N-1)/2.f));
                    sum += ::sum(image.data);
                    projections[i] = Projection(projection, image.size());
                    images[i] = move(image);
                }
                reconstruction.initialize(projections, images);
                window.frameSent = {this, &Tomography::step};
                view.phantom = 0;
                view.volume = &reconstruction.x;
            }
        }
        step();
#if PROFILE
        exit(); return;
#endif
        window.show();
    }
    Random random;
    uint index = 0;
    void step() {
        uint setSize; // one projection per step
        /**/ if(0) setSize = 1;
        else if(0) { setSize=1; for(uint i=2; i <= projections.size/setSize; i++) if(projections.size%i==0) setSize = i; } // few projection per step
        else if(1) setSize = projections.size; // all projections per step
        const uint setCount = this->projections.size / setSize;

        buffer<Projection> projections {setSize};
        buffer<ImageF> images {setSize}; images.clear();

        for(uint i: range(setSize)) {
            uint setIndex = i*setCount+index;
            assert_(index<this->projections.size);
            projections[i] = this->projections[setIndex];
            images[i] = share(this->images[setIndex]);
        }

        if(0) index = (index+1) % setCount; // Sequential order
        if(0) index = random % setCount; // Random order

        if( !reconstruction.step(projections, images) ) window.frameSent = function<void()>();

        if(view.volume == &reconstruction.x) window.render();
    }
} tomography;
