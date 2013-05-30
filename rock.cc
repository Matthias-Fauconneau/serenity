#include "thread.h"
#include "process.h"
#include "volume-operation.h"
#include "time.h"
#include "window.h"
#include "display.h"
#include "render.h"
#include "png.h"

//FIXME: parse module dependencies without these dummy headers
#include "source.h"
#include "capsule.h"
#include "smooth.h"
#include "threshold.h"
#include "distance.h"
#include "skeleton.h"
PASS(Tile, uint16, tile);
#include "floodfill.h"
#include "rasterize.h"
#include "validate.h"
PASS(SquareRoot, uint16, squareRoot);
#include "export.h"

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : PersistentProcess, Widget {
    TEXT(rock) // Rock process definition (embedded in binary)
    Rock(const ref<ref<byte>>& args) : PersistentProcess(rock(), args) {
        ref<byte> resultFolder = "dev/shm"_; // Folder where histograms are written
        ref<byte> result; // Path to file (or folder) where target volume data is copied (only for ASCII export)
        for(const ref<byte>& argument: args) {
            if(argument.contains('=') || &ruleForOutput(argument)) continue;
            if(!arguments.contains("source"_) && (existsFolder(argument) || argument=="validation"_)) { arguments.insert("source"_,argument); continue; }
            if(!result) { result=argument; resultFolder = existsFolder(argument)?argument:section(argument,'/',0,-2); continue; }
            error("Invalid argument"_, argument);
        }
        if(arguments.at("source"_)=="validation"_) {
            ruleForOutput("source"_).operation = "Capsules"_;
            ruleForOutput("pore"_).inputs=array<ref<byte>>({"source"_}); // Skip smooth
        }
        assert_(name, "Usage: rock <source folder containing volume slices> [target] [key=value]*");

        // Configures default arguments
        if(!arguments.contains("cube"_) && arguments.at("source"_)!="validation"_) defaultArguments.insert("cylinder"_,""_); // Clips histograms and slice rendering to the inscribed cylinder
        defaultArguments.insert("kernelSize"_,"1"_);
        if(!result) result = resultFolder = "dev/shm"_;
        arguments.insert("resultFolder"_,resultFolder);

        execute();
        for(const shared<Result>& target: targetResults) if(target->data.size) { current = share( target ); break; }
        assert_(current);

        if(result!="dev/shm"_) { // Copies result to disk
            Time time;
            if(existsFolder(result)) writeFile(current->name+"."_+current->metadata, current->data, resultFolder), log(result+"/"_+current->name+"."_+current->metadata, time);
            else writeFile(result, current->data, root()), log(result, time);
        }
        if(current->data.size==0 || current->name=="ascii"_ || arguments.value("view"_,"0"_)=="0"_) { exit(); return; }
        // Displays result
        window = unique<Window>(this,int2(-1,-1),"Rock"_);
        window->localShortcut(Key('r')).connect(this, &Rock::refresh);
        window->localShortcut(PrintScreen).connect(this, &Rock::saveSlice);
        window->localShortcut(Escape).connect(&exit);
        window->clearBackground = false;
        updateView();
        window->show();
    }

    void refresh() {
        current->timestamp = 0;
        current = getVolume(current->name, arguments);
        updateView();
    }

    bool mouseEvent(int2 cursor, int2 size, Event unused event, Button button) {
        if(button==WheelDown||button==WheelUp) {
            array<ref<byte>> volumes;
            for(const shared<Result>& target: targetResults) if(target->data.size) volumes << target->name;
            int index = clip<int>(0,volumes.indexOf(current->name)+(button==WheelUp?1:-1),volumes.size-1);
            current = share(*targetResults.find(volumes[index]));
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
        window->setTitle(current->name+"*"_);
        assert(current);
        Volume volume = toVolume(current);
        int2 size = volume.sampleCount.xy();
        while(2*size<displaySize) size *= 2;
        if(window->size != size) window->setSize(size);
        else window->render();
        window->setTitle(current->name);
    }

    void render(int2 position, int2 size) {
        assert(current);
        Volume volume = toVolume(current);
        if(volume.sampleSize==20) { exit(); return; } // Don't try to display ASCII
        if(renderVolume) {
            mat3 view;
            view.rotateX(rotation.y); // pitch
            view.rotateZ(rotation.x); // yaw
            shared<Result> empty = getVolume("empty"_, arguments);
            shared<Result> density = getVolume("density"_, arguments);
            shared<Result> intensity = getVolume("intensity"_, arguments);
            Time time;
            assert_(position==int2(0) && size == framebuffer.size());
            ::render(framebuffer, toVolume(empty), toVolume(density), toVolume(intensity), view);
#if PROFILE
            log((uint64)time,"ms");
            window->render(); // Force continuous updates (even when nothing changed)
            wait.reset();
#endif
        } else
        {
            Image image = slice(volume, sliceZ, arguments.contains("cylinder"_));
            while(2*image.size()<=size) image=upsample(image);
            blit(position, image);
        }
    }

    void saveSlice() {
        string path = name+"."_+current->name+"."_+current->metadata+".png"_;
        writeFile(path, encodePNG(slice(toVolume(current),sliceZ,arguments.contains("cylinder"_))), home());
        log(path);
    }

    shared<Result> current;
    float sliceZ = 1./2; // Normalized z coordinate of the currently shown slice
    unique<Window> window {unique<Window>::null()};

    bool renderVolume = false;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
} app ( arguments() );
