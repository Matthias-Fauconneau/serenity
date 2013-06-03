#include "thread.h"
#include "process.h"
#include "volume.h"
#include "volume-operation.h"
#include "time.h"
#include "window.h"
#include "display.h"
#include "render.h"
#include "png.h"

//#include "source.h"
//include "capsule.h"
//#include "smooth.h"
//#include "threshold.h"
//#include "distance.h"
//#include "skeleton.h"
PASS(Tile, uint16, tile);
//#include "floodfill.h"
//#include "rasterize.h"
//#include "validate.h"
PASS(SquareRoot, uint16, squareRoot);
//#include "export.h"

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : PersistentProcess, Widget {
    FILE(rock) // Rock process definition (embedded in binary)
    Rock(const ref<ref<byte>>& args) : PersistentProcess(rock(), args) {
        const Folder& cwd = currentWorkingDirectory(); // Reference for relative paths
        ref<byte> result; // Path to file (or folder) where targets are copied
        for(const ref<byte>& argument: args) {
            if(argument.contains('=') || &ruleForOutput(argument)) continue;
            if(!arguments.contains("source"_) && (existsFolder(argument,cwd) || argument=="validation"_)) { arguments.insert("source"_,argument); continue; }
            if(!result) { result=argument; continue; }
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

        execute();

        if(result) { // Copies result to result folder (on disk)
            if(targetResults.size>1) {
                assert(!existsFile(result,cwd), "New folder would overwrite existing file", result);
                if(!existsFolder(result,cwd)) Folder(result,cwd,true);
            }
            for(const shared<Result>& target: targetResults) if(target->data.size) {
                Time time;
                if(existsFolder(result, cwd)) writeFile(target->name+"."_+target->metadata, target->data, result), log(result+"/"_+target->name+"."_+target->metadata, time);
                else writeFile(result, target->data, cwd), log(target->name+"."_+target->metadata,"->",result, "["_+str(target->data.size/1024/1024)+" MiB]"_, time);
            }
        }

        for(const shared<Result>& target: targetResults) if(target->data.size) { current = share( target ); break; }
        if(!current || current->data.size==0 || current->name=="ascii"_ || arguments.value("view"_,"0"_)=="0"_) { exit(); return; }
        // Displays result
        window = unique<Window>(this,int2(-1,-1),"Rock"_);
        window->localShortcut(Key('r')).connect(this, &Rock::refresh);
        window->localShortcut(PrintScreen).connect(this, &Rock::saveSlice);
        window->localShortcut(Escape).connect([](){exit();});
        window->clearBackground = false;
        updateView();
        window->show();
    }

    void refresh() {
        current->timestamp = 0;
        current = getResult(current->name, arguments);
        updateView();
    }

    bool mouseEvent(int2 cursor, int2 size, Event unused event, Button button) {
        if(button==WheelDown||button==WheelUp) {
            map<ref<byte>, const shared<Result>*> volumes;
            for(const shared<Result>& target: targetResults) if(target->data.size) volumes.insert(target->name, &target);
            int index = clip<int>(0,volumes.keys.indexOf(current->name)+(button==WheelUp?1:-1),volumes.size()-1);
            current = share(*volumes.values[index]);
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
            shared<Result> empty = getResult("empty"_, arguments);
            shared<Result> density = getResult("density"_, arguments);
            shared<Result> intensity = getResult("intensity"_, arguments);
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
