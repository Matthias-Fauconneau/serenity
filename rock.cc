#include "process.h"
#include "time.h"
#include "tiff.h"
#include "window.h"
#include "display.h"
#include "sample.h"
#include "render.h"
#include "operation.h"
#include "capsule.h"
#include "png.h"
#include "smooth.h"
#include "threshold.h"
#include "distance.h"
#include "skeleton.h"
#include "rasterize.h"
#include "maximum.h"

Operation Source("source"_,2); // Loads from original image slices
Operation ShiftRight ("source"_,"shift"_,2); // Shifts data to avoid overflows
Operation SmoothX("shift"_,"smoothx"_,2); // Denoises data and filters small pores by averaging samples in a window (X pass)
Operation SmoothY("smoothx"_,"smoothy"_,2);  // Y pass
Operation SmoothZ("smoothy"_,"smooth"_,2); // Z pass
Operation Threshold("smooth"_,"pore"_,4, "rock"_,4); // Segments between either rock or pore space by comparing density against a uniform threshold
Operation DistanceX("pore"_,"distancex"_,4,"positionxx"_,2); // Computes distance field to nearest rock wall (X pass)
Operation DistanceY("distancex"_,"positionxx"_,"distancey"_,4,"positionyx"_,2,"positionyy"_,2); // Y pass
Operation DistanceZ("distancey"_,"positionyx"_,"positionyy"_,"distance"_,4,"positionx"_,2,"positiony"_,2,"positionz"_,2); // Z pass
//Operation Interleave("positionx"_,"positiony"_,"positionz"_,"position"_,6); //TODO: tile and interleave positions for faster skeleton
Operation Skeleton("positionx"_,"positiony"_,"positionz"_,"skeleton"_,2); // Keeps only voxels on the medial axis of the pore space (integer medial axis skeleton ~ centers of maximal spheres)
Operation Tile("skeleton"_,"tiled_skeleton"_,2);
Operation Rasterize("tiled_skeleton"_,"maximum"_,2); // Rasterizes a ball around each skeleton voxel assigning the radius of the largest enclosing sphere to all voxels

Operation Colorize("pore"_,"source"_,"colorize"_,3); // Maps intensity to either red or green channel depending on binary classification

Operation EmptyX("rock"_,"emptyx"_,4,"epositionx"_,2); // Computes distance field to nearest pore for empty space skipping (X pass)
Operation EmptyY("emptyx"_,"epositionx"_,"emptyy"_,4,"epositionyx"_,2,"epositionyy"_,2); // Y pass
Operation EmptyZ("emptyy"_,"epositionyx"_,"epositionyy"_,"emptyz"_,4,"epositionx"_,2,"epositiony"_,2,"epositionz"_,2); // Z pass
Operation RenderEmpty("emptyz"_,"empty"_,1); // Square roots and tiles distance field before using it for empty space skiping during rendering
Operation RenderDensity("distance"_,"density"_,1); // Square roots and normalizes distance to use as density values (for opacity and gradient)
//Operation RenderDensity("skeleton"_,"density"_,1); // Square roots and normalizes distance to use as density values (for opacity and gradient)
Operation RenderIntensity("maximum"_,"intensity"_,1); // Square roots and normalizes maximum to use as intensity values (for color intensity)

Operation ASCII("maximum"_,"ascii"_,20); // Converts to ASCII (one voxel per line, explicit coordinates)

struct VolumeData {
    VolumeData(const ref<byte>& name):name(name){}
    string name;
    Map map;
    Volume volume;
    //uint referenceCount=0; //TODO: Counts how many operations need this volume (allows memory to be recycled when no operations need the data anymore)
};
template<> string str(const VolumeData& a) { return copy(a.name); }
bool operator ==(const VolumeData& a, const ref<byte>& name) { return a.name == name; }

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : Widget {
    Rock(const ref<ref<byte>>& arguments) {
        // Parses command line arguments
        ref<byte> target; array<ref<byte>> force;
        for(const ref<byte>& argument: arguments) {
            if(argument.contains('=')) { this->arguments.insert(section(argument,'=',0,1), section(argument,'=',1,-1)); continue; } // Stores generic argument to be parsed in relevant operation
            if(operationForOutput(argument)) { // Parses target (or intermediate data to be removed)
                if(!target) target=argument;
                else force << argument;
                continue;
            }
            if(!source && argument=="validation"_) { source=argument, name=argument; continue; }
            if(existsFolder(argument)) {
                if(!source) { source=argument; name=source.contains('/')?section(source,'/',-2,-1):source; continue; }
                if(!result) { result=argument; resultFolder = argument; continue; }
            }
            if(!result) { result=argument; resultFolder = section(argument,'/',0,-2); continue; }
            error("Invalid argument"_, argument);
        }
        if(!result) result = "ptmp"_;
        if(!target) target=operations.last()->name;
        if(target!="ascii") {
            if(this->arguments.contains("selection"_)) selection = split(this->arguments.at("selection"_),',');
            if(!selection) selection<<"source"_<<"smooth"_<<"colorize"_<<"distance"_ << "skeleton"_ << "maximum"_;
            if(!selection.contains(target)) selection<<target;
        }
        if(target=="intensity"_) renderVolume=true;
        cylinder = arguments.contains("cylinder"_) || (existsFolder(source) && !arguments.contains("cube"_));
        assert_(name);

        for(const string& path: memoryFolder.list(Files)) { // Maps intermediate data from any previous run
            if(!startsWith(path, name)) continue;
            VolumeData data = section(path,'.',-3,-2);
            bool remove = !operationForOutput(data.name) || volumes.contains(data.name);
            for(ref<byte> name: force) if(operationForOutput(name)->outputs.contains(data.name)) remove=true;
            if(remove) { ::remove(path, memoryFolder); continue; } // Removes invalid, multiple or to be removed data
            parseVolumeFormat(data.volume, path);
            File file = File(path, memoryFolder, ReadWrite);
            data.map = Map(file, Map::Prot(Map::Read|Map::Write));
            data.volume.data = buffer<byte>(data.map);
            data.volume.sampleSize = data.volume.data.size / data.volume.size();
            assert(data.volume.sampleSize >= align(8, nextPowerOfTwo(log2(nextPowerOfTwo((data.volume.maximum+1))))) / 8); // Minimum sample size to encode maximum value (in 2ⁿ bytes)
            volumes << move(data);
        }

        // Executes all operations
        current = getVolume(target);

        if(target=="ascii"_) { // Writes result to disk
            Time time;
            string volumeName = name+"."_+current->name+"."_+volumeFormat(current->volume);
            if(existsFolder(result)) writeFile(volumeName, current->volume.data, resultFolder), log(result+"/"_+volumeName, time);
            else writeFile(result, current->volume.data, root()), log(result, time);
            if(selection) current = getVolume(selection.last());
            else { exit(); return; }
        }
        // Displays result
        window.localShortcut(Key('r')).connect(this, &Rock::refresh);
        window.localShortcut(PrintScreen).connect(this, &Rock::saveSlice);
        window.localShortcut(Escape).connect(&exit);
        window.clearBackground = false;
        updateView();
        window.show();
    }
    ~Rock() {
#if 0
        for(const string& path: memoryFolder.list(Files)) { // Cleanups intermediate data
            if(!startsWith(path, name)) continue;
            ref<byte> name = section(path,'.',-3,-2);
            if(!operationForOutput(name) || trash.contains(name)) { ::remove(path, memoryFolder); continue; } // Removes invalid or trashed data
        }
#endif
    }

    /// Computes target volume
    const VolumeData* getVolume(const ref<byte>& targetName) {
        for(const VolumeData& data: volumes) if(data.name == targetName) return &data;
        const Operation& operation = *operationForOutput(targetName);
        assert_(&operation, targetName);
        array<const VolumeData*> inputs;
        for(const ref<byte>& input: operation.inputs) { assert(input != targetName, input, targetName); inputs << getVolume( input ); }

        array<VolumeData*> outputs;
        for(const Operation::Output& output: operation.outputs) {
            VolumeData data = output.name;
            Volume& volume = data.volume;
            if(!operation.inputs) { // Original source slices format
                if(name=="validation"_) {
                    volume.x = 512, volume.y = 512, volume.z = 512;
                } else {
                    Folder folder(source);
                    array<string> slices = folder.list(Files);
                    Map file (slices.first(), folder);
                    const Tiff16 image (file);
                    volume.x = image.width, volume.y = image.height, volume.z = slices.size;
                    maxX = volume.x, maxY = volume.y, maxZ = volume.z;
                    if(arguments.contains("cylinder"_)) {
                        auto coordinates = toIntegers(arguments.at("cylinder"_));
                        int x=coordinates[0], y=coordinates[1], r=coordinates[2]; minZ=coordinates[3], maxZ=coordinates[4];
                        minX=x-r, minY=y-r, maxX=x+r, maxY=y+r;
                    }
                    if(arguments.contains("cube"_)) {
                        auto coordinates = toIntegers(arguments.at("cube"_));
                        minX=coordinates[0], minY=coordinates[1], minZ=coordinates[2], maxX=coordinates[3], maxY=coordinates[4], maxZ=coordinates[5];
                    }
                    assert_(minX<maxX && minY<maxY && minZ<maxZ && maxX<=volume.x && maxY<=volume.y && maxZ<=volume.z);
                    volume.x = maxX-minX, volume.y = maxY-minY, volume.z = maxZ-minZ;
                }
                volume.maximum = (1<<(8*output.sampleSize))-1;
            } else { // Inherit initial format from previous operation
                const Volume& source = inputs.first()->volume;
                volume.x=source.x, volume.y=source.y, volume.z=source.z, volume.copyMetadata(source);
            }
            volume.sampleSize = output.sampleSize;
            assert(volume.size() * volume.sampleSize);
            if(volumes.contains(output.name)) {
                rename(name+"."_+output.name+"."_+volumeFormat(volumes[volumes.indexOf(output.name)]->volume), name+"."_+output.name, memoryFolder);
                volumes.removeAll(output.name);
            } else if(trash.contains(output.name)) {
                rename(name+"."_+output.name+"."_+volumeFormat(trash[trash.indexOf(output.name)]->volume), name+"."_+output.name, memoryFolder);
                trash.removeAll(output.name);
            } else {
                assert(!existsFile(name+"."_+output.name+"."_+volumeFormat(volume), memoryFolder), output.name, volumes);
                for(uint i: range(trash.size)) { // Tries to recycle pages (avoid zeroing)
                    const VolumeData& data = trash[i];
                    string path = name+"."_+data.name+"."_+volumeFormat(data.volume);
                    assert_(existsFile(path, memoryFolder), path);
                    rename(path, name+"."_+output.name, memoryFolder);
                    trash.removeAt(i);
                    break;
                }
            }
            // Creates (or resizes) and maps a volume file for the current operation data
            File file(name+"."_+output.name, memoryFolder, Flags(ReadWrite|Create));
            file.resize( volume.size() * volume.sampleSize );
            data.map = Map(file, Map::Prot(Map::Read|Map::Write));
            volume.data = buffer<byte>(data.map);
            assert(volume && volume.data.size == volume.size()*volume.sampleSize);
            volumes << move(data);
            outputs << &volumes.last();
        }
        assert_(outputs);
        Volume& target = outputs.first()->volume;
        uint X = target.x, Y = target.y, Z = target.z, XY=X*Y;

        Time time;
        if(operation==Source) {
            if(name == "validation"_) {
                array<Capsule> capsules = randomCapsules(X,Y,Z, 1);
                rasterize(target, capsules);
                Sample analytic;
                for(Capsule p : capsules) {
                    if(p.radius>=analytic.size) analytic.grow(p.radius+1);
                    analytic[p.radius] += PI*p.radius*p.radius*(4./3*p.radius + norm(p.b-p.a));
                }
                writeFile(name+".analytic.tsv"_, toASCII(analytic), resultFolder);
            } else {
                Folder folder(source);
                Time report;
                array<string> slices = folder.list(Files);
                assert_(slices.size>=maxZ);
                uint16* const targetData = (Volume16&)target;
                for(uint z=0; z<Z; z++) {
                    if(report/1000>=2) { log(z,"/",Z, (z*XY*2/1024/1024)/(time/1000), "MB/s"); report.reset(); } // Reports progress every 2 second (initial read from a cold drive may take minutes)
                    Tiff16(Map(slices[minZ+z],folder)).read(targetData+z*XY, minX, minY, maxX-minX, maxY-minY); // Directly decodes slice images into the volume
                }
            }
        } else {
            const Volume& source = inputs.first()->volume;
            if(operation==ShiftRight || operation==SmoothX || operation==SmoothY || operation==SmoothZ) {
                uint filterSize = arguments.contains("smooth"_) ? toInteger(arguments.at("smooth"_)) : name=="validation"_ ? 0 : 1; // Smooth operation averages samples in a (2×filterSize+1)³ window
                uint sampleCount = 2*filterSize+1;
                uint shift = log2(sampleCount);
                if(operation==ShiftRight) {
                    int max = ((((target.maximum*sampleCount)>>shift)*sampleCount)>>shift)*sampleCount;
                    int bits = log2(nextPowerOfTwo(max));
                    int headroomShift = ::max(0,bits-16);
                    if(headroomShift) log("Shifting out",headroomShift,"least significant bits to compute sum of",sampleCount,"samples without unpacking to 32bit");
                    shiftRight(target, source, headroomShift); // Simply copies if shift = 0
                    target.maximum >>= headroomShift;
                } else if(operation==SmoothX || operation==SmoothY || operation==SmoothZ) {
                    target.maximum *= sampleCount;
                    if(operation==SmoothZ) shift=0; // not necessary
                    smooth(target, source, X,Y,Z, filterSize, shift);
                    target.maximum >>= shift;
                    int margin = target.marginY + align(4, filterSize);
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
                    //writeFile(name+".density.tsv"_, toASCII(density), resultFolder);
                    if(name != "validation"_) density[0]=density[density.size-1]=0; // Ignores clipped values
#if 1 // Lorentzian peak mixture estimation. Works for well separated peaks (intersection under half maximum), proper way would be to use expectation maximization
                    bool plot=false;
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
                perpendicularBisectorEuclideanDistanceTransform(target, outputs[1]->volume, source, X,Y,Z);
            }
            else if(operation==DistanceY || operation==EmptyY) {
                perpendicularBisectorEuclideanDistanceTransform(target, outputs[1]->volume, outputs[2]->volume, source, inputs[1]->volume, X,Y,Z);
            }
            else if(operation==DistanceZ || operation==EmptyZ) {
                perpendicularBisectorEuclideanDistanceTransform(target, outputs[1]->volume, outputs[2]->volume, outputs[3]->volume, source, inputs[1]->volume, inputs[2]->volume, X,Y,Z);
                target.maximum=maximum((const Volume32&)target);
            }
            else if(operation==Tile) tile(target, source);
            else if(operation==Skeleton) integerMedialAxis(target, inputs[0]->volume, inputs[1]->volume, inputs[2]->volume);
            else if(operation==Rasterize) rasterize(target, source);
            else if(operation==ASCII) toASCII(target, source);
            else if(operation==RenderEmpty) squareRoot(target, source);
            else if(operation==RenderDensity || operation==RenderIntensity) ::render(target, source);
            else error("Unimplemented",operation);
        }
        log(operation, time);
        if(target.sampleSize==2) assert(maximum((const Volume16&)target)<=target.maximum, operation, target, maximum((const Volume16&)target), target.maximum);
        if(target.sampleSize==4) assert(maximum((const Volume32&)target)<=target.maximum, operation, target, maximum((const Volume32&)target), target.maximum);

        for(VolumeData* output: outputs) {
            Volume& target = output->volume;
            if(output->name=="distance"_ || output->name=="emptyz"_) // Only packs after distance (or empty) pass (FIXME: make all operations generic)
            while(target.maximum < (1ul<<(8*(target.sampleSize/2))) && target.sampleSize>2/*FIXME*/) { // Packs outputs if needed
                const Volume32& target32 = target;
                target.sampleSize /= 2;
                Time time;
                pack(target, target32);
                log("pack", time);
                output->map.unmap();
                File file(name+"."_+output->name, memoryFolder, ReadWrite);
                file.resize( target.size() * target.sampleSize);
                output->map = Map(file, Map::Prot(Map::Read|Map::Write));
                target.data = buffer<byte>(output->map);
            }
            assert(target.maximum< (1ul<<(8*target.sampleSize)));

            string outputName = name+"."_+output->name;
            rename(outputName, outputName+"."_+volumeFormat(output->volume), memoryFolder); // Renames output files (once data is valid)

            if((output->name=="maximum"_ || output->name=="walk"_) && (1 || !existsFile(outputName+".tsv"_, resultFolder))) {
                Time time;
                Sample histogram = sqrtHistogram(output->volume, cylinder);
                histogram[0] = 0; // Clears background (rock) voxel count to plot with a bigger Y scale
                float scale = toDecimal(arguments.value("resolution"_,"1"_));
                writeFile(outputName+".tsv"_, toASCII(histogram, scale), resultFolder);
                log("√histogram", output->name, time);
            }
        }
        // Recycles all inputs (avoid zeroing new pages) (FIXME: prevent recycling of inputs also being used by other pending operations)
        if(operation != *operations.last()) for(const VolumeData* input: inputs) if(!selection.contains(input->name)) trash << volumes.take(volumes.indexOf(input->name));

        return &volumes[volumes.indexOf(targetName)];
    }

    void refresh() {
        remove(name+"."_+current->name+"."_+volumeFormat(current->volume), memoryFolder);
        string target = copy(current->name);
        int unused index = volumes.removeOne(current);
        assert_(index>=0);
        current = getVolume(target);
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
        assert(current);
        int2 size(current->volume.x,current->volume.y);
        while(2*size<displaySize) size *= 2;
        if(window.size != size) window.setSize(size);
        else window.render();
    }

    void render(int2 position, int2 size) {
        assert(current);
        const Volume& source = current->volume;
        if(source.sampleSize==20) { exit(); return; } // Don't try to display ASCII
        if(!renderVolume) {
            Image image = slice(source, sliceZ, cylinder);
            while(2*image.size()<=size) image=upsample(image);
            blit(position, image);
        } else {
            mat3 view;
            view.rotateX(rotation.y); // pitch
            view.rotateZ(rotation.x); // yaw
            const Volume& empty = getVolume("empty"_)->volume;
            const Volume& density = getVolume("density"_)->volume;
            const Volume& intensity = getVolume("intensity"_)->volume;
            Time time;
            assert_(position==int2(0) && size == framebuffer.size());
            ::render(framebuffer, empty, density, intensity, view);
#if 0
            log((uint64)time,"ms");
            window.render(); // Force continuous updates (even when nothing changed)
            wait.reset();
#endif
        }
    }

    void saveSlice() {
        string path = name+"."_+current->name+"."_+volumeFormat(current->volume)+".png"_;
        writeFile(path, encodePNG(slice(current->volume,sliceZ)), home());
        log(path);
    }

    // Arguments
    ref<byte> source; // Path to folder containing source slice images (or the special token "validation")
    uint minX=0, minY=0, minZ=0, maxX=0, maxY=0, maxZ=0; // Coordinates to crop source volume
    bool cylinder = false; // Whether to clip histograms computation and slice rendering to the inscribed cylinder
    Folder memoryFolder = "dev/shm"_; // Should be a RAM (or local disk) filesystem large enough to hold up to 2 intermediate operations of volume data (up to 32bit per sample)
    ref<byte> name; // Used to name intermediate and output files (folder base name)
    Folder resultFolder = "ptmp"_; // Folder where smoothed density and pore size histograms are written
    ref<byte> result; // Path to file (or folder) where target volume data is copied
    array<ref<byte>> selection; // Data selection for reviewing (mouse wheel selection) (also prevent recycling)
    map<ref<byte>,ref<byte>> arguments;

    // Variables
    array<unique<VolumeData>> volumes; // Mapped volume with valid data
    array<unique<VolumeData>> trash; // Mapped volume with valid data, not being needed anymore, ready to be recycled
    const VolumeData* current = 0;
    float sliceZ = 1./2; // Normalized z coordinate of the currently shown slice
    Window window {this,int2(-1,-1),"Rock"_};

    bool renderVolume = false;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
} app( arguments() );
