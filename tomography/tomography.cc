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
    const uint N = 128;
    const uint P = N; // * threadCount; // Exact adjoint method (gather, scatter) has same space requirement as approximate adjoint method (gather, gather) when P ~ TN
    Phantom phantom {16};
    VolumeF source = phantom.volume(N);
    buffer<Projection> projections {P};
    buffer<ImageF> images {P};
    unique<Reconstruction> reconstructions[1] {unique<CGNR>(N)};
    UniformGrid<View> views{{{0, &reconstructions[0]->x}}};
    View& view = views.last();
#define WINDOW 1
#if WINDOW
    Window window {&views, int2(views.count()*1024,1024), "Tomography"_};
#endif
    //~Music() { writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"ondemand"_); }
    Tomography() {
        //execute("/bin/sh"_, /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"performance"_);

        // Projects phantom
        for(uint i: range(projections.size)) {
            mat4 projection = mat4().rotateX(-PI/2 /*Pitch*/).rotateZ(2*PI*i/N /*Yaw*/);
            ImageF image = float((N-1)/2) * phantom.project(N, projection.scale(vec3(N-1)/2.f));
            projections[i] = Projection(projection, image.size());
            images[i] = move(image);
        }

        for(auto& reconstruction: reconstructions) reconstruction->initialize(projections, images);
        //view.volume = &((CGNR*)reconstructions[0].pointer)->p;
        step();

#if WINDOW
        window.actions[Escape] = []{ exit(); };
        window.background = Window::NoBackground;
        window.show();
        window.displayed = {this, &Tomography::step};
#else
        for(uint unused i: range(2)) step();
#endif
    }

    void step() {
        // Step forward the reconstruction which consumed the least time.
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        reconstructions[index]->step(projections, images);
#if WINDOW
        if(window.target) { // Renders only the reconstruction which updated
            Rect rect = views.layout(window.size)[index];
            assert_(window.target);
            views[index].render(clip(window.target, rect));
            window.putImage(rect);
        }
#endif
    }
} tomography;
