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
#include "smooth.h"
#include "threshold.h"
#include "distance.h"
#include "skeleton.h"
#include "rasterize.h"
class(Tile, Operation), virtual VolumePass<uint16> { void execute(map<ref<byte>, Variant>&, Volume16& target, const Volume& source) override {  tile(target, source); } };
#include "validate.h"
class(SquareRoot, Operation), virtual VolumePass<uint8> { void execute(map<ref<byte>, Variant>&, Volume8& target, const Volume& source) override { squareRoot(target, source); } };
class(ToASCII, Operation), virtual VolumePass<Line> { void execute(map<ref<byte>, Variant>&, VolumeT<Line>& target, const Volume& source) override { toASCII(target, source); } };

#if 0
//Validation
#include "capsule.h"
//volume.x = 512, volume.y = 512, volume.z = 512;
else if(operation==Validate) validate(target, inputs[0]->volume, inputs[1]->volume);
array<Capsule> capsules = randomCapsules(X,Y,Z, 1);
rasterize(target, capsules);
Sample analytic;
for(Capsule p : capsules) {
    if(p.radius>=analytic.size) analytic.grow(p.radius+1);
    analytic[p.radius] += PI*p.radius*p.radius*(4./3*p.radius + norm(p.b-p.a));
}
writeFile(name+".analytic.tsv"_, toASCII(analytic), resultFolder);

else if(operation==SourceASCII || operation==MaximumASCII) toASCII(target, source);
#endif

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : PersistentProcess, Widget {
    TEXT(rock); // Rock process definition (embedded in binary)
    Rock(const ref<ref<byte>>& args) : PersistentProcess(rock(), args) {
        ref<byte> source; // Path to folder containing source slice images (or the special token "validation")
        ref<byte> resultFolder = "ptmp"_; // Folder where histograms are written
        ref<byte> result; // Path to file (or folder) where target volume data is copied
        for(const ref<byte>& argument: args) {
            if(argument.contains('=') || ruleForOutput(argument)) continue;
            if(!name) name=argument;
            if(existsFolder(argument)) {
                if(!source) { source=argument; name=source.contains('/')?section(source,'/',-2,-1):source; continue; }
                if(!result) { result=argument; resultFolder = argument; continue; }
            }
            if(!result) { result=argument; resultFolder = section(argument,'/',0,-2); continue; }
            error("Invalid argument"_, argument);
        }
        if(source) arguments.insert("source"_,source);
        else source=arguments.at("source"_); // or default to validation ?

        // Configures default arguments
        if(!arguments.contains("cube"_) && !arguments.contains("cylinder"_)) // Clip histograms computation and slice rendering to the full inscribed cylinder by default
            arguments.insert("cylinder"_,""_); //FIXME: not for validation
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
