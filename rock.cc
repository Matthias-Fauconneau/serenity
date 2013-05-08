#include "process.h"
#include "time.h"
#include "tiff.h"
#include "window.h"
#include "display.h"
#include "render.h"
#include "histogram.h"
#include "operation.h"
#include "smooth.h"
#include "threshold.h"
#include "distance.h"
#include "maximum.h"
#include "skeleton.h"
#include "rasterize.h"

Operation Source("source"_,2); // Loads from original image slices
Operation ShiftRight ("source"_,"shift"_,2); // Shifts data to avoid overflows
Operation SmoothX("shift"_,"smoothx"_,2); // Denoises data and filters small pores by averaging samples in a window (X pass)
Operation SmoothY("smoothx"_,"smoothy"_,2);  // Y pass
Operation SmoothZ("smoothy"_,"smoothz"_,2); // Z pass
Operation Threshold("smoothz"_,"threshold"_,4); // Segments in rock vs pore space by comparing to a fixed threshold
#if 1
Operation DistanceX("threshold"_,"distancex"_,4); // Computes field of distance to nearest rock wall (X pass)
Operation DistanceY("distancex"_,"distancey"_,4); // Y pass
Operation DistanceZ("distancey"_,"distance"_,4); // Z pass
#if 1
Operation Rasterize("distance"_,"maximum"_,2); // Rasterizes each distance field voxel as a sphere (with maximum blending)
#else
Operation Tile("distance"_,"tile"_,2); // Layouts volume in Z-order to improve locality on maximum search
Operation Maximum("tile"_,"maximum"_,2); // Computes field of nearest local maximum of distance field (i.e field of maximum enclosing sphere radii)
#endif
#else
Operation DistanceX("threshold"_,"distancex"_,4,"positionx"_,2); // Computes field of distance to nearest rock wall (X pass)
Operation DistanceY("distancex"_,"distancey"_,4,"positiony"_,2); // Y pass
Operation DistanceZ("distancey"_,"distancez"_,4,"positionz"_,2); // Z pass
Operation Skeleton("positionx"_,"positiony"_,"positionz"_,"skeleton"_,2); // Computes integer medial axis skeleton
Operation Rasterize("skeleton"_,"maximum"_,2); // Rasterizes skeleton (maximum spheres)
#endif
Operation Crop("maximum"_,"crop"_,2); // Copies a small sample from the center of the volume
Operation ASCII("crop"_,"ascii"_,20); // Converts to ASCII (one voxel per line, explicit coordinates)
Operation Render("maximum"_,"render"_,1); // Copies voxels inside inscribed cylinder, set minimum value to 1 (light attenuation for fake ambient occlusion)

struct VolumeData {
    VolumeData(const ref<byte>& name):name(name){}
    string name;
    Map map;
    Volume volume;
};
bool operator ==(const VolumeData& a, const ref<byte>& name) { return a.name == name; }

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : Widget {
    Rock(const ref<byte>& path, const ref<byte>& target, const ref<ref<byte>>& force) : folder(path), name(section(path,'/',-2,-1)), renderVolume(target=="render"_) {
        assert(name);
        for(const string& path: memoryFolder.list(Files)) { // Maps intermediate data from any previous run
            if(!startsWith(path, name)) continue;
            VolumeData data = section(path,'.',-3,-2);
            if(!operationForOutput(data.name) || volumes.contains(data.name) || force.contains(data.name) || force.contains(operationForOutput(data.name)->name)) { remove(path, memoryFolder); continue; } // Removes invalid, multiple or forced data
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
                array<string> slices = folder.list(Files);
                Map file (slices.first(), folder);
                const Tiff16 image (file);
                volume.x = image.width, volume.y = image.height, volume.z = slices.size, volume.den = (1<<(8*output.sampleSize))-1;
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
            array<string> slices = folder.list(Files);
            Time time;
            uint16* const targetData = (Volume16&)target;
            for(uint z=0; z<slices.size; z++) {
                if(time/1000>=2) { log(z,"/",slices.size); time.reset(); } // Reports progress every 2 second (initial read from a cold drive may take minutes)
                Tiff16(Map(slices[z],folder)).read(targetData+z*XY); // Directly decodes slice images into the volume
            }
        } else {
            const Volume& source = inputs.first()->volume;

            if(operation==ShiftRight || operation==SmoothX || operation==SmoothY || operation==SmoothZ) {
                constexpr int sampleCount = 2*filterSize+1;
                constexpr uint shift = log2(sampleCount);
                if(operation==ShiftRight) {
                    int max = (((((target.den/target.num)*sampleCount)>>shift)*sampleCount)>>shift)*sampleCount;
                    int bits = log2(nextPowerOfTwo(max));
                    int shift = ::max(0,bits-16);
                    if(shift) log("Shifting out",shift,"least significant bits to compute sum of",sampleCount,"samples without unpacking to 32bit");
                    shiftRight(target, source, shift); // Simply copies if shift = 0
                    target.den >>= shift;
                }
                else if(operation==SmoothX) {
                    smooth(target, source, X,Y,Z, filterSize, shift);
                    target.den *= sampleCount; target.den >>= shift;
                    target.marginX += align(4, filterSize);
                }
                else if(operation==SmoothY) {
                    smooth(target, source, Y,Z,X, filterSize, shift);
                    target.den *= sampleCount; target.den >>= shift;
                    target.marginY += align(4, filterSize);
                }
                else if(operation==SmoothZ) {
                    smooth(target, source, Z,X,Y, filterSize, 0);
                    target.den *= sampleCount;
                    target.marginZ += align(4, filterSize);
                }
            }
            else if(operation==Threshold) {
                if(1 || !existsFile(name+".density.tsv"_, resultFolder)) { // Computes density histogram of smoothed volume
                    Time time;
                    writeFile(name+".density.tsv"_, str(histogram(source)), resultFolder);
                    log("density", time);
                }
                Histogram density = parseHistogram( readFile(name+".density.tsv"_, resultFolder) );
                // Use the minimum between the two highest maximum of density histogram as density threshold
                struct { uint density=0, count=0; } max[2];
                for(uint i=1; i<density.binCount-1; i++) {
                    if(density[i-1] < density[i] && density[i] > density[i+1] && density[i] > max[0].count) {
                        max[0].density = i, max[0].count = density[i];
                        if(max[0].count > max[1].count) swap(max[0],max[1]);
                    }
                }
                uint densityThreshold; uint minimum = -1;
                for(uint i=max[0].density; i<max[1].density; i++) {
                    if(density[i] < minimum) densityThreshold = i, minimum = density[i];
                }
                log("Using threshold",densityThreshold,"between pore at",max[0].density,"and rock at",max[1].density);
                threshold(target, source, float(densityThreshold) / float(density.binCount));
            }
            else if(operation==DistanceX) {
                perpendicularBisectorEuclideanDistanceTransform<false>(target, /*outputs[1]->volume,*/ source, X,Y,Z/*, 1,X,XY*/);
            }
            else if(operation==DistanceY) {
                perpendicularBisectorEuclideanDistanceTransform<false>(target, /*outputs[1]->volume,*/ source,  Y,Z,X/*, X,XY,1*/);
            }
            else if(operation==DistanceZ) {
                perpendicularBisectorEuclideanDistanceTransform<true>(target, /*outputs[1]->volume,*/ source,  Z,X,Y/*, XY,1,X*/);
                target.num = 1, target.den=maximum((const Volume32&)target);
            }
            //else if(operation==Tile) tile(target, source);
            //else if(operation==Maximum) maximum(target, source);
            //else if(operation==Skeleton) integerMedialAxis(target, inputs[0]->volume, inputs[1]->volume, inputs[2]->volume);
            else if(operation==Rasterize) rasterize(target, source);
            else if(operation==Crop) { const int size=256; crop(target, source, source.x/2-size/2, source.y/2-size/2, source.z/2-size/2, source.x/2+size/2, source.y/2+size/2, source.z/2+size/2); }
            else if(operation==ASCII) toASCII(target, source);
            else if(operation==Render) ::render(target, source);
            else error("Unimplemented",operation);
        }
        log(operation, time);

        for(VolumeData* output: outputs) {
            Volume& target = output->volume;
            if(target.sampleSize==20) continue; // Don't try to pack ASCII
            while(target.den/target.num < (1ul<<(8*(target.sampleSize/2))) && target.sampleSize>2/*FIXME*/) { // Packs outputs if needed
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
            assert(target.den/target.num < (1ul<<(8*target.sampleSize)), target.num, target.den, target.den/target.num, 1ul<<(8*target.sampleSize));

            rename(name+"."_+output->name, name+"."_+output->name+"."_+volumeFormat(output->volume), memoryFolder); // Renames output files (once data is valid)
        }
        // Recycles all inputs (avoid zeroing new pages)
        for(const VolumeData* input: inputs) thrash << volumes.take(volumes.indexOf(input->name));

        if(operation==Source) writeFile(name+".raw_density.tsv"_, str(histogram(target)), resultFolder);
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
            if(event == Press) return true;
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
        int2 size (current->x-2*current->marginX,current->y-2*current->marginY);
        window.setSize(size);
        window.render();
    }

    void render(int2 position, int2) {
        if(current->sampleSize==20) exit(); // Don't try to display ASCII
        assert(current);
        uint z = current->marginZ+(current->z-2*current->marginZ-1)*currentSlice;
        Image image;
        if(!renderVolume) image = current->squared ? squareRoot(*current, z) : slice(*current, z);
        else {
            mat3 view;
            view.rotateX(rotation.y); // pitch
            view.rotateZ(rotation.x); // yaw
            image = ::render(*current, view);
        }
        blit(position, image);
    }

    // Settings
    static constexpr uint filterSize = 8; // Smooth operation averages samples in a (2×filterSize+1)³ window
    const Folder memoryFolder {"dev/shm"_}; // Should be a RAM (or local disk) filesystem large enough to hold up to 2 intermediate operations of volume data (up to 32bit per sample)
    const Folder resultFolder {"ptmp"_}; // Final results (histograms) are written there

    // Arguments
    Folder folder; // Contains source slice images
    string name; // Used to name intermediate and output files (folder base name)

    // Variables
    array<unique<VolumeData>> volumes;
    array<unique<VolumeData>> thrash;
    const Volume* current=0;
    float currentSlice=0; // Normalized z coordinate of the currently shown slice
    Window window {this,int2(-1,-1),"Rock"_};

    bool renderVolume;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
} app( arguments()[0], arguments().size>1 ? arguments()[1] : operations.last()->name, arguments().size>1 ? arguments().slice(1) : array<ref<byte>>() );
