/// \file view.cc Volume data viewer
#include "tool.h"
#include "volume.h"
#include "volume-operation.h"
#include "window.h"
#include "display.h"
#include "render.h"
#include "png.h"

/// Displays all target results
class(View, Tool), Widget {
    buffer<byte> execute(Process& process) override {
        this->process = &process;
        for(const shared<Result>& result: process.targetResults) {
            if(result->data) {
                if(result->metadata=="scalar"_) log_(str(result->name, "=", result->data));
                else if(endsWith(result->metadata,"map"_) || endsWith(result->metadata,".tsv"_)) { // Distribution or scalar map
                    if(count(result->data,'\n')<16) log_(str(result->name, "["_+str(count(result->data,'\n'))+"]"_,":\n"_+result->data));
                }
                else if(inRange(1u,toVolume(result).sampleSize,4u)) { if(!current) current = share(result); } // Displays first displayable volume
                else warn("Unknown format",result->metadata, result->name, result->localArguments);
            } else assert(result->elements, result->name);
        }
        if(current) {
            window = unique<Window>(this,int2(-1,-1),"Rock"_);
            window->localShortcut(Escape).connect([]{exit();});
            window->clearBackground = false;
            updateView();
            window->show();
        }
        return buffer<byte>();
    }
    bool mouseEvent(int2 cursor, int2 size, Event unused event, Button button) {
        if(button==WheelDown||button==WheelUp) {
            int volumeCount=0, volumeIndex=0;
            for(const shared<Result>& result: process->targetResults) if(toVolume(result)) { if(result==current) volumeIndex=volumeCount; volumeCount++; }
            int newVolumeIndex = clip<int>(0,volumeIndex+(button==WheelUp?1:-1),volumeCount-1);
            if(newVolumeIndex == volumeIndex) return true;
            volumeCount = 0;
            for(const shared<Result>& result: process->targetResults) if(toVolume(result)) { if(newVolumeIndex==volumeCount) current=share(result); volumeCount++; }
            updateView();
            return true;
        }
        if(!button) return false;
        /*if(renderVolume) {
            int2 delta = cursor-lastPos;
            lastPos = cursor;
            if(event != Motion) return false;
            rotation += vec2(-2*PI*delta.x/size.x,2*PI*delta.y/size.y);
            rotation.y= clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
        } else*/
        {
            float z = clip(0.f, float(cursor.x)/(size.x-1), 1.f);
            if(sliceZ != z) { sliceZ = z; updateView(); }
        }
        updateView();
        return true;
    }

    void updateView() {
        assert(current);
        Volume volume = toVolume(current);
        int2 size = volume.sampleCount.xy();
        while(2*size<displaySize) size *= 2;
        if(window->size != size) window->setSize(size);
        else window->render();
        window->setTitle(str(current));
    }
    void render(int2 position, int2 size) {
        assert(current);
        Volume volume = toVolume(current);
        if(volume.sampleSize==20) { exit(); return; } // Don't try to display ASCII
        if(volume.sampleSize>4) error(current->name, volume.sampleSize);
        /*if(renderVolume) {
            mat3 view;
            view.rotateX(rotation.y); // pitch
            view.rotateZ(rotation.x); // yaw
            shared<Result> empty = process->getResult("empty"_, arguments);
            shared<Result> density = process->getResult("density"_, arguments);
            shared<Result> intensity = process->getResult("intensity"_, arguments);
            Time time;
            assert_(position==int2(0) && size == framebuffer.size());
            ::render(framebuffer, toVolume(empty), toVolume(density), toVolume(intensity), view);
#if PROFILE
            log((uint64)time,"ms");
            window->render(); // Force continuous updates (even when nothing changed)
            wait.reset();
#endif
        } else*/
        {
            Image image = slice(volume, sliceZ, process->arguments.contains("cylinder"_));
            while(2*image.size()<=size) image=upsample(image);
            blit(position, image);
        }
    }
    const Process* process = 0;
    shared<Result> current;
    float sliceZ = 1./2; // Normalized z coordinate of the currently shown slice
    unique<Window> window = nullptr;

    bool renderVolume = false;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
};

