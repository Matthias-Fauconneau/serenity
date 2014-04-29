#include "phantom.h"
#include "update.h"
#include "adjoint.h"
//include "approximate.h"
#include "plot.h"
#include "window.h"
#include "layout.h"
#include "graphics.h"

/// Projects \a volume onto \a image according to \a projection
inline void project(const ImageF& image, const VolumeF& source, const Projection& projection) {
    const CylinderVolume volume(source);
    parallel(image.height, [&projection, &volume, &source, &image](uint, uint y) {
        v4sf start,end;
        mref<float> row = image.data.slice(y*image.width, image.width);
        for(uint x: range(row.size)) row[x] = intersect(projection, vec2(x, y), volume, start, end) ? project(start, projection.ray, end, volume, source.data) : 0;
    }, coreCount);
}

inline float SSQ(const VolumeF& x) {
    const float* xData = x;
    float SSQ[coreCount] = {};
    chunk_parallel(x.size(), [&](uint id, uint offset, uint size) {
        float accumulator = 0;
        for(uint i: range(offset,offset+size)) accumulator += sq(xData[i]);
        SSQ[id] += accumulator;
    });
    return sum(SSQ);
}

inline float SSE(const VolumeF& a, const VolumeF& b) {
    assert_(a.size() == b.size());
    const float* aData = a; const float* bData = b;
    float SSE[coreCount] = {};
    chunk_parallel(a.size(), [&](uint id, uint offset, uint size) {
        float accumulator = 0;
        for(uint i: range(offset,offset+size)) accumulator += sq(aData[i] - bData[i]);
        SSE[id] += accumulator;
    });
    return sum(SSE);
}

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
        float max = volume->sampleCount.x/2;
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
#if DEBUG
    const uint N = 64;
#else
    const uint N = 128;
#endif
    const uint P = N; // * threadCount; // Exact adjoint method (gather, scatter) has same space requirement as approximate adjoint method (gather, gather) when P ~ TN
    Phantom phantom {16};
    VolumeF x0 = phantom.volume(N);
    buffer<Projection> projections {P};
    buffer<ImageF> images {P};
#if 1 // ADJOINT
    string labels[1] = {"Adjoint"_};
    unique<Reconstruction> reconstructions[1] {unique<Adjoint>(N)};
    HList<View> views{{{0, &reconstructions[0]->x}}};
#elif 1
    string labels[2] = {"Direct"_,"Filtered"_};
    unique<Reconstruction> reconstructions[2] {unique<Adjoint>(N), unique<Adjoint>(N, true)};
    HList<View> views{{{0, &reconstructions[0]->x},{0, &x0},{0, &reconstructions[1]->x}}};
#elif 0 // APPROXIMATE
    string labels[1] = {"Approximate"_};
    unique<Reconstruction> reconstructions[1] {unique<Approximate>(N)};
    HList<View> views{{{0, &reconstructions[0]->x}}};
#else
    string labels[2] = {"Adjoint"_,"Approximate"_};
    unique<Reconstruction> reconstructions[2] {unique<Adjoint>(N), unique<Approximate>(N)};
    HList<View> views{{{0, &reconstructions[0]->x},{0, &x0},{0, &reconstructions[1]->x}}};
#endif
    float SSQ = ::SSQ(x0);
#define WINDOW 1
#if WINDOW
#if PLOT
    Plot plot;
    VBox layout {{&views, &plot}};
#else
    VBox layout {{&views}};
#endif
    Window window {&layout, int2(512), "Tomography"_};
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
        //view.volume = &((Adjoint*)reconstructions[0].pointer)->p;
        step();

#if WINDOW
        window.actions[Escape] = []{ exit(); };
        window.background = Window::NoBackground;
        window.show();
        window.displayed = {this, &Tomography::step};
#else
        for(uint unused i: range(3)) step();
#endif
    }

    void step() {
        // Step forward the reconstruction which consumed the least time.
        uint index = argmin(mref<unique<Reconstruction>>(reconstructions));
        log_(left(labels[index],16)+"\t"_);
        reconstructions[index]->step(projections, images);
        const float PSNR = 10*log10(SSQ / ::SSE(reconstructions[index]->x, x0));
#if PLOT
        plot[labels[index]].insert(reconstructions[index]->totalTime.toFloat(), PSNR);
#endif
        log("\t", PSNR);
#if WINDOW
        if(window.target) { // FIXME
#if PLOT
            // Renders plot
            {Rect rect = layout.layout(window.size)[1];
                Image target = clip(window.target, rect);
                fill(target, Rect(target.size()), white);
                plot.render(target);
            }
#endif
            // Renders only the reconstruction which updated
            uint viewIndex = index ? 2 : 0;
            Rect viewsRect = layout.layout(window.size)[0];
            Rect rect = viewsRect.position() + views.layout(viewsRect.size())[viewIndex];
            Image target = clip(window.target, rect);
            views[viewIndex].render(target);
            window.putImage();
        }
#endif
    }
} tomography;
