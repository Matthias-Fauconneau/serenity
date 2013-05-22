#include "thread.h"
#include "process.h"
#include "volume-operation.h"
#include "time.h"
#include "window.h"
#include "display.h"
//#include "render.h"
#include "png.h"

//FIXME: parse module dependencies without these dummy headers
#include "source.h"
#include "smooth.h"
#include "threshold.h"

/*Operation SmoothX("shift"_,"smoothx"_,2); // Denoises data and filters smallest pores by averaging samples in a window (X pass)
Operation SmoothY("smoothx"_,"smoothy"_,2);  // Y pass
Operation SmoothZ("smoothy"_,"smooth"_,2); // Z pass
Operation Threshold("smooth"_,"pore"_,4, "rock"_,4); // Segments between either rock or pore space by comparing density against a uniform threshold
Operation DistanceX("pore"_,"distancex"_,4,"positionxx"_,2); // Computes distance field to nearest rock wall (X pass)
Operation DistanceY("distancex"_,"positionxx"_,"distancey"_,4,"positionyx"_,2,"positionyy"_,2); // Y pass
Operation DistanceZ("distancey"_,"positionyx"_,"positionyy"_,"distance"_,4,"positionx"_,2,"positiony"_,2,"positionz"_,2); // Z pass
Operation Skeleton("positionx"_,"positiony"_,"positionz"_,"skeleton"_,2); // Keeps only voxels on the medial axis of the pore space (integer medial axis skeleton ~ centers of maximal spheres)
Operation Tile("skeleton"_,"tiled_skeleton"_,2);
Operation Rasterize("tiled_skeleton"_,"maximum"_,2); // Rasterizes a ball around each skeleton voxel assigning the radius of the largest enclosing sphere to all voxels*/

/*Operation Validate("pore"_,"maximum"_,"validate"_,2); // Validates the maximum balls result and helps visualizes filter effects
Operation Colorize("pore"_,"source"_,"colorize"_,3); // Maps intensity to either red or green channel depending on binary classification
Operation EmptyX("rock"_,"emptyx"_,4,"epositionx"_,2); // Computes distance field to nearest pore for empty space skipping (X pass)
Operation EmptyY("emptyx"_,"epositionx"_,"emptyy"_,4,"epositionyx"_,2,"epositionyy"_,2); // Y pass
Operation EmptyZ("emptyy"_,"epositionyx"_,"epositionyy"_,"emptyz"_,4,"epositionx"_,2,"epositiony"_,2,"epositionz"_,2); // Z pass
Operation RenderEmpty("emptyz"_,"empty"_,1); // Square roots and tiles distance field before using it for empty space skiping during rendering
Operation RenderDensity("distance"_,"density"_,1); // Square roots and normalizes distance to use as density values (for opacity and gradient)
Operation RenderIntensity("maximum"_,"intensity"_,1); // Square roots and normalizes maximum to use as intensity values (for color intensity)
Operation SourceASCII("source"_,"source_ascii"_,20); // Converts to ASCII (one voxel per line, explicit coordinates)
Operation MaximumASCII("maximum"_,"maximum_ascii"_,20); // Converts to ASCII (one voxel per line, explicit coordinates)*/

#if 0
// Pack
Volume& target = output->volume;
if(output->name=="distance"_ || output->name=="emptyz"_) // Only packs after distance (or empty) pass (FIXME: make all operations generic)
    while(target.maximum < (1ul<<(8*(target.sampleSize/2))) && target.sampleSize>2) { // Packs outputs if needed
        const Volume32& target32 = target;
        target.sampleSize /= 2;
        Time time;
        pack(target, target32);
        log("pack", time);
        output->map.unmap();
        File file(output->name, storageFolder, ReadWrite);
        file.resize( target.size() * target.sampleSize);
        output->map = Map(file, Map::Prot(Map::Read|Map::Write));
        target.data = buffer<byte>(output->map);
    }
assert(target.maximum< (1ul<<(8*target.sampleSize)));

// Histogram
if((output->name=="maximum"_) && (1 || !existsFile(name+"maximum.tsv"_, resultFolder))) {
    Time time;
    Sample squaredMaximum = histogram(output->volume, cylinder);
    squaredMaximum[0] = 0; // Clears background (rock) voxel count to plot with a bigger Y scale
    float scale = toDecimal(args.value("resolution"_,"1"_));
    writeFile(name+"maximum.tsv"_, toASCII(squaredMaximum, false, true, scale), resultFolder);
    log("√histogram", output->name, time);
}

        else if(operation==Threshold) {

        }

        else if(operation==Colorize) {
            colorize(target, source, inputs[1]->volume);
        }

        else if(operation==DistanceX || operation==EmptyX) {
            perpendicularBisectorEuclideanDistanceTransform(target, outputs[1]->volume, source);
        }
        else if(operation==DistanceY || operation==EmptyY) {
            perpendicularBisectorEuclideanDistanceTransform(target, outputs[1]->volume, outputs[2]->volume, source, inputs[1]->volume);
        }
        else if(operation==DistanceZ || operation==EmptyZ) {
            perpendicularBisectorEuclideanDistanceTransform(target, outputs[1]->volume, outputs[2]->volume, outputs[3]->volume, source, inputs[1]->volume, inputs[2]->volume);
            target.maximum=maximum((const Volume32&)target);
        }

        else if(operation==Tile) tile(target, source);

        else if(operation==Skeleton) {
            uint minimalSqRadius = arguments.contains("minimalRadius"_) ? sqr(toInteger(arguments.at("minimalRadius"_))) : 3;
            integerMedialAxis(target, inputs[0]->volume, inputs[1]->volume, inputs[2]->volume, minimalSqRadius);
        }

        else if(operation==Rasterize) rasterize(target, source);

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
        else if(operation==RenderEmpty) squareRoot(target, source);
        else if(operation==RenderDensity || operation==RenderIntensity) ::render(target, source);
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
        //if(target=="intensity"_) renderVolume=true;
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
#if RENDER
        if(renderVolume) {
            int2 delta = cursor-lastPos;
            lastPos = cursor;
            if(event != Motion) return false;
            rotation += vec2(-2*PI*delta.x/size.x,2*PI*delta.y/size.y);
            rotation.y= clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
        }
#endif
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
#if RENDER
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
#endif
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

#if RENDER
    bool renderVolume = false;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
#endif
} app ( arguments() );
