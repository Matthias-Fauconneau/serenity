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
#include "rasterize.h"
class(Tile, Operation), virtual VolumePass<uint16> { void execute(const map<ref<byte>, Variant>&, Volume16& target, const Volume& source) override {  tile(target, source); } };
#include "validate.h"
class(SquareRoot, Operation), virtual VolumePass<uint8> { void execute(const map<ref<byte>, Variant>&, Volume8& target, const Volume& source) override { squareRoot(target, source); } };
class(ToASCII, Operation), virtual VolumePass<Line> { void execute(const map<ref<byte>, Variant>&, VolumeT<Line>& target, const Volume& source) override { toASCII(target, source); } };

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : PersistentProcess, Widget {
    TEXT(rock) // Rock process definition (embedded in binary)
    Rock(const ref<ref<byte>>& args) : PersistentProcess(rock(), args) {
        ref<byte> resultFolder = "ptmp"_; // Folder where histograms are written
        ref<byte> result; // Path to file (or folder) where target volume data is copied (only for ASCII export)
        for(const ref<byte>& argument: args) {
            if(argument.contains('=') || ruleForOutput(argument)) continue;
            if(!arguments.contains("source"_) && (existsFolder(argument) || argument=="validation"_)) { arguments.insert("source"_,argument); continue; }
            if(!result) { result=argument; resultFolder = existsFolder(argument)?argument:section(argument,'/',0,-2); continue; }
            error("Invalid argument"_, argument);
        }
        if(arguments.at("source"_)=="validation"_) {
            ruleForOutput("source"_)->operation = "Capsules"_;
            ruleForOutput("pore"_)->inputs=array<ref<byte>>({"source"_}); // Skip smooth
        }
        assert_(name);

        // Configures default arguments
        if(!arguments.contains("cube"_) && !arguments.contains("cylinder"_) && arguments.at("source"_)!="validation"_)
            arguments.insert("cylinder"_,""_); // Clip histograms computation and slice rendering to the full inscribed cylinder by default
        if(!arguments.contains("kernelSize"_)) arguments.insert("kernelSize"_, 1);
        if(target!="ascii") {
            if(arguments.contains("selection"_)) selection = split(arguments.at("selection"_),',');
            if(!selection) selection<<"source"_<<"smooth"_<<"colorize"_<<"distance"_ << "skeleton"_ << "maximum"_;
            if(!selection.contains(target)) selection<<target;
        }
        if(target=="intensity"_) renderVolume=true;
        if(!result) result = resultFolder = "ptmp"_;
        arguments.insert("resultFolder"_,resultFolder);

        // Executes all operations
        current = getVolume(target);

        if(target=="ascii"_) { // Writes result to disk
            Time time;
            if(existsFolder(result)) writeFile(current->name+"."_+current->metadata, current->data, resultFolder), log(result+"/"_+current->name+"."_+current->metadata, time);
            else writeFile(result, current->data, root()), log(result, time);
            if(selection) current = getVolume(selection.last());
            else { exit(); return; }
        }
        if(arguments.value("view"_,"1"_)=="0"_) { exit(); return; }
        // Displays result
        window.localShortcut(Key('r')).connect(this, &Rock::refresh);
        window.localShortcut(PrintScreen).connect(this, &Rock::saveSlice);
        window.localShortcut(Escape).connect(&exit);
        window.clearBackground = false;
        updateView();
        window.show();
    }

    void refresh() {
        current->timestamp = 0;
        current = getVolume(current->name);
        updateView();
    }

    bool mouseEvent(int2 cursor, int2 size, Event unused event, Button button) {
        if(button==WheelDown||button==WheelUp) {
            int index = clip<int>(0,selection.indexOf(current->name)+(button==WheelUp?1:-1),selection.size-1);
            current = getVolume(selection[index]);
            updateView();
            return true;
        }
        if(!button) return false;
        if(renderVolume) {
            int2 delta = cursor-lastPos;
            lastPos = cursor;
            if(event != Motion) return false;
            rotation += vec2(-2*PI*delta.x/size.x,2*PI*delta.y/size.y);
            rotation.y= clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
        }
        else {
            float z = clip(0.f, float(cursor.x)/(size.x-1), 1.f);
            if(sliceZ != z) { sliceZ = z; updateView(); }
        }
        updateView();
        return true;
    }

    void updateView() {
        window.setTitle(current->name+"*"_);
        assert(current);
        Volume volume = toVolume(current);
        int2 size(volume.x, volume.y);
        while(2*size<displaySize) size *= 2;
        if(window.size != size) window.setSize(size);
        else window.render();
        window.setTitle(current->name);
    }

    void render(int2 position, int2 size) {
        assert(current);
        Volume volume = toVolume(current);
        if(volume.sampleSize==20) { exit(); return; } // Don't try to display ASCII
        if(renderVolume) {
            mat3 view;
            view.rotateX(rotation.y); // pitch
            view.rotateZ(rotation.x); // yaw
            shared<Result> empty = getVolume("empty"_);
            shared<Result> density = getVolume("density"_);
            shared<Result> intensity = getVolume("intensity"_);
            Time time;
            assert_(position==int2(0) && size == framebuffer.size());
            ::render(framebuffer, toVolume(empty), toVolume(density), toVolume(intensity), view);
#if PROFILE
            log((uint64)time,"ms");
            window.render(); // Force continuous updates (even when nothing changed)
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
        writeFile(path, encodePNG(slice(toVolume(current),sliceZ)), home());
        log(path);
    }

    array<ref<byte>> selection; // Data selection for reviewing (mouse wheel selection) (also prevent recycling)
    shared<Result> current;
    float sliceZ = 1./2; // Normalized z coordinate of the currently shown slice
    Window window {this,int2(-1,-1),"Rock"_};

    bool renderVolume = false;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
} app ( arguments() );
