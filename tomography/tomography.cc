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
        mat4 projection = mat4().rotateX(rotation.y /*Pitch*/).rotateZ(rotation.x /*Yaw*/).scale(norm(target.size())/norm(volume->sampleCount));
        if(phantom) {
            ImageF linear = (volume->sampleCount.x/2) * phantom->project(target.size(), projection.scale(vec3(volume->sampleCount)/2.f));
            convert(target, linear, volume->sampleCount.x);
        } else {
            ImageF linear {target.size()};
            project(linear, *volume, projection);
            convert(target, linear, volume->sampleCount.x);
        }
    }
};

struct Tomography {
    const uint N = 64;
    Phantom phantom {N};
    VolumeF source = phantom.volume(N);
    buffer<Projection> projections {N};
    buffer<ImageF> images {N};
    VolumeF target {N};
    View view {&phantom, &source};
    Window window {&view, int2(704), "Tomography"_};
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
            if(view.volume == &source) view.volume = &target; else view.volume = &source;
            window.render();
        };
        for(string argument: arguments()) {
            if(argument=="view"_) {
                //view.phantom = 0;
                view.volume = &source;
            }
            if(argument=="compute"_) {
                float sum = 0;
                for(uint i: range(N)) { // Projects phantom
                    mat4 projection = mat4().rotateX(-PI/2 /*Pitch*/).rotateZ(2*PI*i/N /*Yaw*/);
                    projections[i] = projection;
                    ImageF image = (target.sampleCount.x/2) * phantom.project(N, projection.scale(vec3(target.sampleCount)/2.f));
                    sum += ::sum(image.data);
                    images[i] = move(image);
                }
                float volume = (PI*sq((target.sampleCount.x-1)/2)*target.sampleCount.z);
                float mean = (sum/N) / volume; // Projection energy / volume of the support
                target.clear(mean);
                step();
                window.frameSent = {this, &Tomography::step};
                view.phantom = 0;
                view.volume = &target;
            }
        }
        window.show();
    }
    Random random;
    uint projectionIndex = 0;
    void step() {
#if 0 // "SART" (one projection per step (i.e subset=1)
        update(target, {projections[projectionIndex]}, images.slice(projectionIndex,1));
#elif 1 // subset SIRT (few projection per step)
        //const uint subsetSize = sqrt(float(projections.size));
        uint subsetSize = 1; for(uint i=2; i <= projections.size/subsetSize; i++) if(projections.size%i==0) subsetSize = i;
        assert_(subsetSize==sqrt(float(N)));
        assert_(projections.size%subsetSize == 0); //FIXME: use nearest divisor (or handle partial subsets)
        buffer<Projection> projections {subsetSize};
        buffer<ImageF> images {subsetSize}; images.clear();

        const uint subsetCount = this->projections.size / subsetSize;
        const uint subsetIndex = projectionIndex / subsetCount;
        for(uint i: range(subsetSize)) {
            uint index = i*subsetCount+subsetIndex;
            assert_(index<this->projections.size);
            projections[i] = this->projections[index];
            images[i] = share(this->images[index]);
        }
        update(target, projections, images);
#else
        // SIRT (all projection per step (i.e subset=N)
        update(target, projections, images);
#endif
#if SEQ
        projectionIndex = (projectionIndex+1)%projections.size; // In order (FIXME: coprime stride nearest to golden ratio)
#else
        projectionIndex = random % projections.size;
#endif
        if(view.volume == &target) window.render();
    }
} tomography;
