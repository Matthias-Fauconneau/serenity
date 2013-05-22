#include "thread.h"
#include "process.h"
#include "time.h"
#include "tiff.h"
#include "window.h"
#include "display.h"
#include "sample.h"
#include "render.h"
#include "capsule.h"
#include "png.h"

Volume toVolume(const Result& result) {
    Volume volume;
    parseVolumeFormat(volume, result.metadata);
    volume.data = buffer<byte>(result.data);
    volume.sampleSize = volume.data.size / volume.size();
    assert(volume.sampleSize >= align(8, nextPowerOfTwo(log2(nextPowerOfTwo((volume.maximum+1))))) / 8); // Minimum sample size to encode maximum value (in 2ⁿ bytes)
    return volume;
}

/// Convenience class to help define volume operations
struct VolumeOperation : virtual Operation {
    virtual ref<int> outputSampleSize() abstract;
    uint64 outputSize(map<ref<byte>, Variant>&, const ref<shared<Result>>& inputs, uint index) override { return toVolume(inputs[0]).size() * outputSampleSize()[index]; }
    virtual void execute(map<ref<byte>, Variant>& args, array<Volume>& outputs, const ref<Volume>& inputs) abstract;
    void execute(map<ref<byte>, Variant>& args, array<shared<Result>>& outputs, const ref<shared<Result>>& inputs) override {
        array<Volume> inputVolumes = apply<Volume>(inputs, toVolume);
        array<Volume> outputVolumes;
        for(uint index: range(outputs.size)) {
            Volume volume;
            volume.sampleSize = outputSampleSize()[index];
            volume.data = buffer<byte>(outputs[index]->data);
            if(inputVolumes) { // Inherits initial metadata from previous operation
                const Volume& source = inputVolumes.first();
                volume.x=source.x, volume.y=source.y, volume.z=source.z, volume.copyMetadata(source);
                assert(volume.sampleSize * volume.size() == volume.data.size);
            }
            outputVolumes << move( volume );
        }
        execute(args, outputVolumes, inputVolumes);
        for(uint index: range(outputs.size)) {
            Result& result = outputs[index];
            Volume& output = outputVolumes[index];
            result.metadata = volumeFormat(output);
            if(output.sampleSize==2) assert(maximum((const Volume16&)output)<=output.maximum, output, maximum((const Volume16&)output), output.maximum);
            if(output.sampleSize==4) assert(maximum((const Volume32&)output)<=output.maximum, output, maximum((const Volume32&)output), output.maximum);
        }
    }
};


/*Volume& target = output->volume;
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

if((output->name=="maximum"_) && (1 || !existsFile(name+"maximum.tsv"_, resultFolder))) {
    Time time;
    Sample squaredMaximum = histogram(output->volume, cylinder);
    squaredMaximum[0] = 0; // Clears background (rock) voxel count to plot with a bigger Y scale
    float scale = toDecimal(args.value("resolution"_,"1"_));
    writeFile(name+"maximum.tsv"_, toASCII(squaredMaximum, false, true, scale), resultFolder);
    log("√histogram", output->name, time);
}*/

/// Loads from original image slices
class(Source, Operation), virtual VolumeOperation {
    uint minX, minY, minZ, maxX, maxY, maxZ;

    ref<int> outputSampleSize() override { return {2}; }

    uint64 outputSize(map<ref<byte>, Variant>& args, const ref<shared<Result>>&, uint) override {
        Folder folder(args.at("source"_));
        array<string> slices = folder.list(Files);
        assert(slices, args.at("source"_));
        Map file (slices.first(), folder);
        const Tiff16 image (file);
        minX=0, minY=0, minZ=0, maxX = image.width, maxY = image.height, maxZ = slices.size;
        if(args.contains("cylinder"_)) {
            auto coordinates = toIntegers(args.at("cylinder"_));
            int x=coordinates[0], y=coordinates[1], r=coordinates[2]; minZ=coordinates[3], maxZ=coordinates[4];
            minX=x-r, minY=y-r, maxX=x+r, maxY=y+r;
        }
        if(args.contains("cube"_)) {
            auto coordinates = toIntegers(args.at("cube"_));
            minX=coordinates[0], minY=coordinates[1], minZ=coordinates[2], maxX=coordinates[3], maxY=coordinates[4], maxZ=coordinates[5];
        }
        assert_(minX<maxX && minY<maxY && minZ<maxZ && maxX<=image.width && maxY<=image.height && maxZ<=slices.size);
        return (maxX-minX)*(maxY-minY)*(maxZ-minZ)*outputSampleSize()[0];
    }

    void execute(map<ref<byte>, Variant>& args, array<Volume>& outputs, const ref<Volume>&) {
        Folder folder(args.at("source"_));
        array<string> slices = folder.list(Files);

        Volume& volume = outputs.first();
        volume.x = maxX-minX, volume.y = maxY-minY, volume.z = maxZ-minZ;
        volume.maximum = (1<<volume.sampleSize)-1;
        uint X = volume.x, Y = volume.y, Z = volume.z, XY=X*Y;
        Time time; Time report;
        uint16* const targetData = (Volume16&)outputs.first();
        for(uint z=0; z<Z; z++) {
            if(report/1000>=2) { log(z,"/",Z, (z*XY*2/1024/1024)/(time/1000), "MB/s"); report.reset(); } // Reports progress every 2 second (initial read from a cold drive may take minutes)
            Tiff16(Map(slices[minZ+z],folder)).read(targetData+z*XY, minX, minY, maxX-minX, maxY-minY); // Directly decodes slice images into the volume
        }
    }
};

//Validation
//volume.x = 512, volume.y = 512, volume.z = 512;

/*#include "smooth.h"
/// Shifts data to avoid overflows
class(ShiftRight, Operation), virtual Pass<uint16, uint16> {
    void execute(Volume& target, const Volume& source, map<ref<byte>, Variant>& args) {
        shiftRight(target, source, args.at("shift"_));
    }
};*/

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

        if(rule==ShiftRight || rule==SmoothX || rule==SmoothY || rule==SmoothZ) {
            uint kernelSize = arguments.contains("smooth"_) ? toInteger(arguments.at("smooth"_)) : name=="validation"_ ? 0 : 1; // Smooth operation averages samples in a (2×kernelSize+1)³ window
            uint sampleCount = 2*kernelSize+1;
            uint shift = log2(sampleCount);
            if(rule==ShiftRight) {
                int max = ((((target.maximum*sampleCount)>>shift)*sampleCount)>>shift)*sampleCount;
                int bits = log2(nextPowerOfTwo(max));
                int headroomShift = ::max(0,bits-16);
                shiftRight(target, source, headroomShift); // Simply copies if shift = 0
                target.maximum >>= headroomShift;
            } else if(operation==SmoothX || operation==SmoothY || operation==SmoothZ) {
                target.maximum *= sampleCount;
                if(operation==SmoothZ) shift=0; // not necessary
                smooth(target, source, kernelSize, shift);
                target.maximum >>= shift;
                int margin = target.marginY + align(4, kernelSize);
                target.marginY = target.marginZ;
                target.marginZ = target.marginX;
                target.marginX = margin;
            }
        }


        else if(operation==Threshold) {
            float densityThreshold=0;
            if(arguments.contains("threshold"_)) {
                densityThreshold = toDecimal(arguments.at("threshold"_));
                while(densityThreshold >= 1) densityThreshold /= 1<<8; // Accepts 16bit, 8bit or normalized threshold
            }
            if(!densityThreshold) {
                Time time;
                Sample density = histogram(source,  cylinder);
                log("density", time);
                bool plot = false;
                if(plot) writeFile(name+".density.tsv"_, toASCII(density), resultFolder);
                if(name != "validation"_) density[0]=density[density.size-1]=0; // Ignores clipped values
#if 1 // Lorentzian peak mixture estimation. Works for well separated peaks (intersection under half maximum), proper way would be to use expectation maximization
                Lorentz rock = estimateLorentz(density); // Rock density is the highest peak
                if(plot) writeFile(name+".rock.tsv"_, toASCII(sample(rock,density.size)), resultFolder);
                Sample notrock = density - sample(rock, density.size); // Substracts first estimated peak in order to estimate second peak
                if(plot) writeFile(name+".notrock.tsv"_, toASCII(notrock), resultFolder);
                Lorentz pore = estimateLorentz(notrock); // Pore density is the new highest peak
                pore.height = density[pore.position]; // Use peak height from total data (estimating on not-rock yields too low estimate because rock is estimated so wide its tail overlaps pore peak)
                if(plot) writeFile(name+".pore.tsv"_, toASCII(sample(pore,density.size)), resultFolder);
                Sample notpore = density - sample(pore, density.size);
                if(plot) writeFile(name+".notpore.tsv"_, toASCII(notpore), resultFolder);
                uint threshold=0; for(uint i: range(pore.position, rock.position)) if(pore[i] <= notpore[i]) { threshold = i; break; } // First intersection between pore and not-pore (same probability)
                if(name=="validation"_) threshold = (pore.position+rock.position)/2; // Validation threshold cannot be estimated with this model
                densityThreshold = float(threshold) / float(density.size);
                log("Automatic threshold", densityThreshold, "between pore at", float(pore.position)/float(density.size), "and rock at", float(rock.position)/float(density.size));
#else // Exhaustively search for inter-class variance maximum ω₁ω₂(μ₁ - μ₂)² (shown by Otsu to be equivalent to intra-class variance minimum ω₁σ₁² + ω₂σ₂²)
                uint threshold=0; double maximum=0;
                uint64 totalCount=0, totalSum=0; double totalMaximum=0;
                for(uint64 t: range(density.size)) totalCount+=density[t], totalSum += t * density[t], totalMaximum=max(totalMaximum, double(density[t]));
                uint64 backgroundCount=0, backgroundSum=0;
                Sample interclass (density.size, density.size, 0);
                double variances[density.size];
                for(uint64 t: range(density.size)) {
                    backgroundCount += density[t];
                    if(backgroundCount == 0) continue;
                    backgroundSum += t*density[t];
                    uint64 foregroundCount = totalCount - backgroundCount, foregroundSum = totalSum - backgroundSum;
                    if(foregroundCount == 0) break;
                    double variance = double(foregroundCount)*double(backgroundCount)*sqr(double(foregroundSum)/double(foregroundCount) - double(backgroundSum)/double(backgroundCount));
                    if(variance > maximum) maximum=variance, threshold = t;
                    variances[t] = variance;
                }
                for(uint t: range(density.size)) interclass[t]=variances[t]*totalMaximum/maximum; // Scales to plot over density
                writeFile("interclass.tsv"_,toASCII(interclass), resultFolder);
                densityThreshold = float(threshold) / float(density.size);
                log("Automatic threshold", threshold, densityThreshold);
#endif
            } else log("Manual threshold", densityThreshold);
            threshold(target, outputs[1]->volume, source, densityThreshold);
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

        else if(operation==Validate) validate(target, inputs[0]->volume, inputs[1]->volume);
    //Validation
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

TEXT(rock); // Rock process definition (embedded in binary)

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : PersistentProcess, Widget {
    Rock(const ref<ref<byte>>& args) : PersistentProcess(rock, args) {
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
        if(!result) result = "ptmp"_;
        if(target!="ascii") {
            if(arguments.contains("selection"_)) selection = split(arguments.at("selection"_),',');
            if(!selection) selection<<"source"_<<"smooth"_<<"colorize"_<<"distance"_ << "skeleton"_ << "maximum"_;
            if(!selection.contains(target)) selection<<target;
        }
        if(target=="intensity"_) renderVolume=true;

        // Executes all operations
        current = getVolume(target);

        if(target=="ascii"_) { // Writes result to disk
            Time time;
            if(existsFolder(result)) writeFile(current->name+"."_+current->metadata, current->data, resultFolder), log(result+"/"_+current->name+"."_+current->metadata, time);
            else writeFile(result, current->data, root()), log(result, time);
            if(selection) current = getVolume(selection.last());
            else { exit(); return; }
        }
        if(arguments.value("view"_,"1"_)!="0"_) { exit(); return; }
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

    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) {
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
        } else {
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
        if(!renderVolume) {
            // Whether to clip histograms computation and slice rendering to the inscribed cylinder
            bool cylinder = arguments.contains("cylinder"_) || (existsFolder(source) && !arguments.contains("cube"_));
            Image image = slice(volume, sliceZ, cylinder);
            while(2*image.size()<=size) image=upsample(image);
            blit(position, image);
        } else {
            mat3 view;
            view.rotateX(rotation.y); // pitch
            view.rotateZ(rotation.x); // yaw
            shared<Result> empty = getVolume("empty"_);
            shared<Result> density = getVolume("density"_);
            shared<Result> intensity = getVolume("intensity"_);
            Time time;
            assert_(position==int2(0) && size == framebuffer.size());
            ::render(framebuffer, toVolume(empty), toVolume(density), toVolume(intensity), view);
#if 0
            log((uint64)time,"ms");
            window.render(); // Force continuous updates (even when nothing changed)
            wait.reset();
#endif
        }
    }

    void saveSlice() {
        string path = name+"."_+current->name+"."_+current->metadata+".png"_;
        writeFile(path, encodePNG(slice(toVolume(current),sliceZ)), home());
        log(path);
    }

    ref<byte> source; // Path to folder containing source slice images (or the special token "validation")
    Folder resultFolder = "ptmp"_; // Folder where smoothed density and pore size histograms are written
    ref<byte> result; // Path to file (or folder) where target volume data is copied
    array<ref<byte>> selection; // Data selection for reviewing (mouse wheel selection) (also prevent recycling)
    shared<Result> current;
    float sliceZ = 1./2; // Normalized z coordinate of the currently shown slice
    Window window {this,int2(-1,-1),"Rock"_};

    bool renderVolume = false;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
} app( arguments() );
