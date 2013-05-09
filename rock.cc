#include "process.h"
#include "time.h"
#include "tiff.h"
#include "window.h"
#include "display.h"
#include "histogram.h"
#include "render.h"
#include "operation.h"
#include "validation.h"
#include "smooth.h"
#include "threshold.h"
#include "distance.h"
#include "rasterize.h"

Operation Source("source"_,2); // Loads from original image slices
Operation ShiftRight ("source"_,"shift"_,2); // Shifts data to avoid overflows
Operation SmoothX("shift"_,"smoothx"_,2); // Denoises data and filters small pores by averaging samples in a window (X pass)
Operation SmoothY("smoothx"_,"smoothy"_,2);  // Y pass
Operation SmoothZ("smoothy"_,"smooth"_,2); // Z pass
Operation Threshold("smooth"_,"pore"_,4, "rock"_,4); // Segments in rock vs pore space by comparing to a fixed threshold
#if 1
Operation DistanceX("pore"_,"distancex"_,4); // Computes distance field to nearest rock (X pass)
Operation DistanceY("distancex"_,"distancey"_,4); // Y pass
Operation DistanceZ("distancey"_,"distance"_,4); // Z pass
Operation Rasterize("distance"_,"maximum"_,2); // Rasterizes each distance field voxel as a ball (with maximum blending)
#else
Operation FeatureTransformX("threshold"_,"positionx"_,2); // Computes position of nearest rock wall (X pass)
Operation FeatureTransformY("positionx"_,"positiony"_,2); // Y pass
Operation FeatureTransformZ("positiony"_,"positionz"_,2); // Z pass
Operation Skeleton("positionx"_,"positiony"_,"positionz"_,"skeleton"_,2); // Computes integer medial axis skeleton
Operation Rasterize("skeleton"_,"maximum"_,2); // Rasterizes each distance field voxel as a ball (with maximum blending)
#endif

Operation EmptyX("rock"_,"emptyx"_,4); // Computes distance field to nearest pore for empty space skipping (X pass)
Operation EmptyY("emptyx"_,"emptyy"_,4); // Y pass
Operation EmptyZ("emptyy"_,"emptyz"_,4); // Z pass
Operation RenderEmpty("emptyz"_,"empty"_,1); // Square roots and tiles distance field before using it for empty space skiping during rendering
Operation RenderDensity("distance"_,"density"_,1); // Square roots and normalizes distance to use as density values (for opacity and gradient)
Operation RenderIntensity("maximum"_,"intensity"_,1); // Square roots and normalizes maximum to use as intensity values (for color intensity)

Operation ASCII("maximum"_,"ascii"_,20); // Converts to ASCII (one voxel per line, explicit coordinates)

struct VolumeData {
    VolumeData(const ref<byte>& name):name(name){}
    string name;
    Map map;
    Volume volume;
    //uint referenceCount=0; //TODO: Counts how many operations need this volume (allows memory to be recycled when no operations need the data anymore)
};
bool operator ==(const VolumeData& a, const ref<byte>& name) { return a.name == name; }

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : Widget {
    Rock(const ref<ref<byte>>& arguments) {
        ref<byte> target;
        for(const ref<byte>& argument: arguments) {
            if(argument.contains('=')) { this->arguments.insert(section(argument,'=',0,1), section(argument,'=',1,-1)); continue; } // Stores generic argument to be parsed in relevant operation
            if(!target && operationForOutput(argument)) { target=argument; log("target",target); if(target=="intensity"_) renderVolume=true; continue; }
            if(existsFolder(argument)) {
                if(!source) { source=argument; log("source",source); name=section(source,'/',-2,-1); continue; }
                if(!ascii) { ascii=argument; resultFolder = argument; log("ascii",ascii); continue; }
            }
            if(!ascii) { ascii=argument; resultFolder = section(argument,'/',0,-2); log("ascii",ascii); continue; }
            error("Invalid command line arguments"_, arguments);
        }
        if(!target) target=operations.last()->name;
        assert(name);
        for(const string& path: memoryFolder.list(Files)) { // Maps intermediate data from any previous run
            if(!startsWith(path, name)) continue;
            VolumeData data = section(path,'.',-3,-2);
            if(!operationForOutput(data.name) || volumes.contains(data.name) || target==data.name) { remove(path, memoryFolder); continue; } // Removes invalid, multiple or target data
            parseVolumeFormat(data.volume, path);
            File file = File(path, memoryFolder, ReadWrite);
            data.map = Map(file, Map::Prot(Map::Read|Map::Write));
            data.volume.data = buffer<byte>(data.map);
            assert( data.volume );
            volumes << move(data);
        }
        current = &getVolume(target)->volume;
        updateView();

        window.localShortcut(Escape).connect(&exit);
        window.clearBackground = false;
        window.show();
    }

    /// Computes target volume
    const VolumeData* getVolume(const ref<byte>& targetName) {
        for(const VolumeData& data: volumes) if(data.name == targetName) return &data;
        const Operation& operation = *operationForOutput(targetName);
        assert_(&operation, targetName);
        array<const VolumeData*> inputs;
        for(const ref<byte>& input: operation.inputs) inputs << getVolume( input );

        array<VolumeData*> outputs;
        for(const Operation::Output& output: operation.outputs) {
            VolumeData data = output.name;
            Volume& volume = data.volume;
            if(!operation.inputs) { // Original source slices format
                if(name == "balls"_) {
                    volume.x = 1024, volume.y = 1024, volume.z = 1024;
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
            assert(!existsFile(name+"."_+output.name+"."_+volumeFormat(volume), memoryFolder), output.name); // Would have been loaded
            for(uint i: range(thrash.size)) { // Tries to recycle pages (avoid zeroing)
                const VolumeData& data = thrash[i];
                string path = name+"."_+data.name+"."_+volumeFormat(data.volume);
                assert_(existsFile(path, memoryFolder), path);
                rename(path, name+"."_+output.name, memoryFolder);
                thrash.removeAt(i);
                break;
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
        Volume& target = outputs.first()->volume;
        uint X = target.x, Y = target.y, Z = target.z, XY=X*Y;

        Time time;
        if(operation==Source) {
            if(name == "balls"_) {
                randomBalls(target);
                // TODO: write analytic histogram
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
                int sampleCount = 2*filterSize+1;
                uint shift = log2(sampleCount);
                if(operation==ShiftRight) {
                    int max = ((((target.maximum*sampleCount)>>shift)*sampleCount)>>shift)*sampleCount;
                    int bits = log2(nextPowerOfTwo(max));
                    int shift = ::max(0,bits-16);
                    if(shift) log("Shifting out",shift,"least significant bits to compute sum of",sampleCount,"samples without unpacking to 32bit");
                    shiftRight(target, source, shift); // Simply copies if shift = 0
                    target.maximum >>= shift;
                }
                else if(operation==SmoothX) {
                    smooth(target, source, X,Y,Z, filterSize, shift);
                    target.maximum *= sampleCount; target.maximum >>= shift;
                    target.marginX += align(4, filterSize);
                }
                else if(operation==SmoothY) {
                    smooth(target, source, Y,Z,X, filterSize, shift);
                    target.maximum *= sampleCount; target.maximum >>= shift;
                    target.marginY += align(4, filterSize);
                }
                else if(operation==SmoothZ) {
                    smooth(target, source, Z,X,Y, filterSize, 0);
                    target.maximum *= sampleCount;
                    target.marginZ += align(4, filterSize);
                }
            }
            else if(operation==Threshold) {
                float densityThreshold=0;
                if(arguments.contains("threshold"_)) {
                    densityThreshold = toDecimal(arguments.at("threshold"_));
                    while(densityThreshold >= 1) densityThreshold /= 1<<8; // Accepts 16bit, 8bit or normalized threshold
                }
                if(!densityThreshold) {
                    if(1 || !existsFile(name+".density.tsv"_, resultFolder)) { // Computes density histogram of smoothed volume
                        Time time;
                        Histogram density = histogram(source);
                        log("density", time.reset());
                        {Histogram smoothDensity (density.size, density.size);
                            int filterSize=density.size/0x100; // Box smooth histogram
                            log(filterSize);
                            for(uint i=filterSize; i<density.size-filterSize; i++) {
                                uint sum=0;
                                for(int di=-filterSize; di<+filterSize; di++) sum+=density[i+di];
                                smoothDensity[i] = sum/(2*filterSize);
                            }
                            density = move(smoothDensity);
                            log("smooth", time.reset());
                        }
                        writeFile(name+".density.tsv"_, str(density), resultFolder);
                        log("write", time.reset());
                    }
                    Histogram density = parseHistogram( readFile(name+".density.tsv"_, resultFolder) );
                    // Use the minimum between the two highest maximum of density histogram as density threshold
                    struct { uint density=0, count=0; } max[2];
                    for(uint i=1; i<density.size-1; i++) {
                        if(density[i-1] < density[i] && density[i] > density[i+1] && density[i] > max[0].count) {
                            max[0].density = i, max[0].count = density[i];
                            if(max[0].count > max[1].count) swap(max[0],max[1]);
                        }
                    }
                    uint threshold=0; uint minimum = -1;
                    for(uint i=max[0].density; i<max[1].density; i++) {
                        if(density[i] < minimum) threshold = i, minimum = density[i];
                    }
                    densityThreshold = float(threshold) / float(density.size);
                    log("Automatic threshold", densityThreshold, "between pore at", float(max[0].density)/float(density.size), "and rock at", float(max[1].density)/float(density.size));
                } else log("Manual threshold", densityThreshold);
                threshold(target, outputs[1]->volume, source, densityThreshold);
            }
            else if(operation==DistanceX || operation==EmptyX) {
                perpendicularBisectorEuclideanDistanceTransform<false>(target, source, X,Y,Z);
            }
            else if(operation==DistanceY || operation==EmptyY) {
                perpendicularBisectorEuclideanDistanceTransform<false>(target, source, Y,Z,X);
            }
            else if(operation==DistanceZ || operation==EmptyZ) {
                perpendicularBisectorEuclideanDistanceTransform<true>(target, source, Z,X,Y);
                target.maximum=maximum((const Volume32&)target);
            }
            //else if(operation==Tile) tile(target, source);
            //else if(operation==Maximum) maximum(target, source);
            //else if(operation==Skeleton) integerMedialAxis(target, inputs[0]->volume, inputs[1]->volume, inputs[2]->volume);
            else if(operation==Rasterize) rasterize(target, source);
            //else if(operation==Crop) { const int size=256; crop(target, source, source.x/2-size/2, source.y/2-size/2, source.z/2-size/2, source.x/2+size/2, source.y/2+size/2, source.z/2+size/2); }
            else if(operation==ASCII) toASCII(target, source);
            else if(operation==RenderEmpty) squareRoot(target, source);
            else if(operation==RenderDensity || operation==RenderIntensity) ::render(target, source);
            else error("Unimplemented",operation);
        }
        log(operation, time);

        for(VolumeData* output: outputs) {
            Volume& target = output->volume;
            if(target.sampleSize==20) continue; // Don't try to pack ASCII
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

            rename(name+"."_+output->name, name+"."_+output->name+"."_+volumeFormat(output->volume), memoryFolder); // Renames output files (once data is valid)
        }
        // Recycles all inputs (avoid zeroing new pages) (FIXME: prevent recycling of inputs also being used by other pending operations)
        if(operation != *operations.last()) for(const VolumeData* input: inputs) thrash << volumes.take(volumes.indexOf(input->name));

        if(operation==Rasterize && (1 || !existsFile(name+".radius.tsv"_, resultFolder))) {
            Histogram histogram = sqrtHistogram(target);
            histogram[0] = 0; // Clears background (rock) voxel count to plot with a bigger Y scale
            writeFile(name+".radius.tsv"_, str(histogram), resultFolder);
            log("Pore size histogram written to",name+".radius.tsv"_);
        }
        return &volumes[volumes.indexOf(targetName)];
    }

    /// Shows an image corresponding to the volume slice at Z position \a index
    void setSlice(float slice) {
        slice = clip(0.f, slice, 1.f);
        if(currentSlice != slice) { currentSlice = slice; updateView(); }
    }

    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) {
        if(renderVolume) {
            if(!button) return false;
            int2 delta = cursor-lastPos;
            lastPos = cursor;
            if(event != Motion) return false;
            rotation += vec2(-2*PI*delta.x/size.x,2*PI*delta.y/size.y); //TODO: warp
            rotation.y= clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
        } else {
            setSlice(float(cursor.x)/(size.x-1));
        }
        updateView();
        return true;
    }

    void updateView() {
        assert(current);
        int2 size(current->x-2*current->marginX,current->y-2*current->marginY);
        if(window.size != size) window.setSize(size);
        else window.render();
    }

    void render(int2 position, int2 size) {
        if(current->sampleSize==20) { exit(); return; } // Don't try to display ASCII
        assert(current);
        uint z = current->marginZ+(current->z-2*current->marginZ-1)*currentSlice;
        if(!renderVolume) {
            Image image = current->squared ? squareRoot(*current, z) : slice(*current, z);
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

    // Arguments
    ref<byte> source; // Path to folder containing source slice images (or name of a validation case (balls, cylinders, cones))
    uint minX=0, minY=0, minZ=0, maxX=0, maxY=0, maxZ=0; // Coordinates to crop source volume
    Folder memoryFolder = "dev/shm"_; // Should be a RAM (or local disk) filesystem large enough to hold up to 2 intermediate operations of volume data (up to 32bit per sample)
    ref<byte> name; // Used to name intermediate and output files (folder base name)
    uint filterSize = 8; // Smooth operation averages samples in a (2×filterSize+1)³ window
    Folder resultFolder = "ptmp"_; // Folder where smoothed density and pore size histograms are written
    ref<byte> ascii; // Path to file (or folder) where ASCII encoded pore size volume is copied
    map<ref<byte>,ref<byte>> arguments;

    // Variables
    array<unique<VolumeData>> volumes; // Mapped volume with valid data
    array<unique<VolumeData>> thrash; // Mapped volume with valid data, not being needed anymore, ready to be recycled
    const Volume* current=0;
    float currentSlice=0; // Normalized z coordinate of the currently shown slice
    Window window {this,int2(-1,-1),"Rock"_};

    bool renderVolume = false;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
} app( arguments() );
