#include "process.h"
#include "time.h"
#include "tiff.h"
#include "smooth.h"
#include "threshold.h"
#include "histogram.h"
#include "distance.h"
#include "maximum.h"
#include "window.h"
#include "interface.h"
//#include "render.h"

enum Pass {
    Null,  // No data
    Source, // Loads from original image slices
    Smooth, // Denoises data and filters small pores by averaging samples in a window
    Threshold, // Segments in rock vs pore space by comparing to a fixed threshold (local density minimum around 0.45)
    DistanceX, // Computes field of distance to nearest rock wall (X pass)
    DistanceY, // Y pass
    DistanceZ, // Z pass
    Tile, // Layouts volume in Z-order to improve locality on maximum search
    Maximum // Computes field of nearest local maximum of distance field (i.e field of maximum enclosing sphere radii)
};
static constexpr uint passSampleSize[] = {0,2,2,4,4,4,4,2,2}; // Sample size for each pass to correctly allocate necessary space
static constexpr ref<byte> passNames[] = {"null"_, "source"_,"smooth"_,"threshold"_,"distancex"_,"distancey"_,"distancez"_,"tile"_,"maximum"_};
const ref<byte>& str(Pass pass) { return passNames[(uint)pass]; }
Pass toPass(const ref<byte>& pass) { return (Pass)max(0,ref<ref<byte>>(passNames).indexOf(pass)); }

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : Widget {
    Rock(const ref<byte>& path, Pass force) : folder(path), name(section(path,'/',-2,-1)), force(force) {
        assert(name);
        for(const string& path: memoryFolder.list(Files)) { // Maps intermediate data from any previous run
            if(find(path, ".null."_)) { remove(path, memoryFolder); continue; } // Cleanup any aborted pass data
            if(!startsWith(path, name)) continue;
            ref<byte> passName = section(path,'.',-3,-2);
            Pass pass = toPass( passName );
            if(force==Null || pass < force) {
                assert(!previous.pass);
                swap(previous, current);
                current.pass = pass;
                File file = File(path, memoryFolder, ReadWrite);
                current.map = Map(file, Map::Prot(Map::Read|Map::Write));
                Volume& target = current.volume;
                parseVolumeFormat(target, path);
                target.data = buffer<byte>(current.map);
                assert( target );
                if(previous.pass>current.pass) swap(previous, current);
            } else remove(path, memoryFolder);
        }
        if(previous.pass>Null) log("Using",previous.pass,current.pass>Null?str("and",current.pass):""_,"from disk");
        setPass(force != Null ? force : Maximum);

        window.localShortcut(Escape).connect(&exit);
        window.clearBackground = false;
        window.show();
    }

    /// Executes pipeline until \a pass
    void setPass(Pass pass) {
        assert(pass > Null);
        if(pass==current.pass) return;
        if(pass==previous.pass) swap(previous, current); // Toggles view betwen previous and current passes
        else {
            if(current.pass<previous.pass) swap(previous, current); // Ensures current pass > previous pass (Reverts any previous toggle)
            if(pass < current.pass) swap(previous, current); // Restarts from previous pass
            if(pass < current.pass) previous=move(current), current.pass=Null; // Restarts from scratch
            while(current.pass<pass) next();
        }
        updateView();
    }

    /// Executes next pass of the pipeline
    void next() {
        string nullPath = name+".null."_+volumeFormat(current.volume);
        if(previous.pass) rename(name+"."_+str(previous.pass)+"."_+volumeFormat(previous.volume), nullPath, memoryFolder); // Invalidates previous file before overwriting it
        swap(previous, current); // After swap, previous is the source (referencing old current), and current is the target (overwriting old previous) (i.e double buffering)
        Pass pass = Pass(int(previous.pass)+1);
        Volume& source = previous.volume;
        Volume& target = current.volume;
        target.x=source.x, target.y=source.y, target.z=source.z, target.copyMetadata(source); // Inherit initial format from previous pass
        target.sampleSize = passSampleSize[pass];
        assert(!existsFile(name+"."_+str(pass)+"."_+volumeFormat(target), memoryFolder));

        array<string> slices; // only used by Source pass
        if(!target || target.data.size != target.size()*target.sampleSize) { // Creates (or resizes) and maps a volume file for the current pass data
            current.map.unmap();
            if(pass==Source) {
                slices = folder.list(Files);
                Map file (slices.first(), folder);
                const Tiff16 image (file);
                target.x = image.width, target.y = image.height, target.z = slices.size, target.den = (1<<16)-1;
            }
            assert(target.size());
            File file(nullPath, memoryFolder, Flags(ReadWrite|Create));
            file.resize( target.size() * target.sampleSize );
            current.map = Map(file, Map::Prot(Map::Read|Map::Write));
            target.data = buffer<byte>(current.map);
        }
        assert(target.data.size == target.size()*target.sampleSize);

        Time time;
        if(pass==Source) {
            Time time;
            uint XY=target.x*target.y;
            uint16* const targetData = (Volume16&)target;
            for(uint z=0; z<slices.size; z++) {
                if(time/1000>=2) { log(z,"/",slices.size); time.reset(); } // Reports progress every 2 second (initial read from a cold drive may take minutes)
                Tiff16(Map(slices[z],folder)).read(targetData+z*XY); // Directly decodes slice images into the volume
            }
        }
        else if(pass==Smooth) {
            smooth<smoothFilterSize>(target, source);
        }
        else if(pass==Threshold) {
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
        else if(pass==DistanceX) {
            PerpendicularBisectorEuclideanDistanceTransform<false>(target, source, source.x,source.y,source.z);
            log(maximum((const Volume32&)target));
        }
        else if(pass==DistanceY) {
            PerpendicularBisectorEuclideanDistanceTransform<false>(target, source,  source.y,source.z,source.x);
            log(maximum((const Volume32&)target));
        }
        else if(pass==DistanceZ) {
            PerpendicularBisectorEuclideanDistanceTransform<true>(target, source,  source.z,source.x,source.y);
            target.num = 1, target.den=maximum((const Volume32&)target);
            log(maximum((const Volume32&)target));
        }
        else if(pass==Tile) {
            tile(target, source);
        }
        else if(pass==Maximum) {
            maximum(target, source);
        }
        log(pass, time);

        while(target.den/target.num < (1ul<<(8*(target.sampleSize/2)))) { // Packs target if needed
            log(target.den, target.num, target.den/target.num, (1ul<<(8*(target.sampleSize/2))));
            const Volume32& target32 = target;
            target.sampleSize /= 2;
            Time time;
            pack(target, target32);
            log("pack", time);
            current.map.unmap();
            File file(nullPath, memoryFolder, ReadWrite);
            file.resize( target.size() * target.sampleSize);
            current.map = Map(file, Map::Prot(Map::Read|Map::Write));
        }
        assert(target.den/target.num < (1ul<<(8*target.sampleSize)), target.num, target.den, target.den/target.num, 1ul<<(8*target.sampleSize));

        current.pass = pass;
        string path = name+"."_+str(pass)+"."_+volumeFormat(target);
        rename(nullPath, path, memoryFolder); // Renames target file to the current pass (once data is valid)

        if(pass==Source) {
            writeFile(name+".raw_density.tsv"_, str(histogram(target)), resultFolder);
        }
        if(pass==Maximum && (1 || !existsFile(name+".radius.tsv"_, resultFolder))) {
            Histogram histogram = sqrtHistogram(target);
            histogram[0] = 0; // Clears rock space voxel count to plot with a bigger Y scale
            writeFile(name+".radius.tsv"_, str(histogram), resultFolder);
            log("Pore size histogram written to",name+".radius.tsv"_);
        }
    }

    /// Shows an image corresponding to the volume slice at Z position \a index
    void setSlice(float slice) {
        slice = clip(0.f, slice, 1.f);
        if(currentSlice != slice) { currentSlice = slice; updateView(); }
    }

    bool mouseEvent(int2 cursor, int2 size, Event, Button button) {
        if(button==WheelDown) setPass((Pass)max((int)Source,current.pass-1));
        if(button==WheelUp) setPass((Pass)min((int)Maximum,current.pass+1));
        setSlice(float(cursor.x)/(size.x-1));
#if RENDER
        QPoint delta = ev->pos()-lastPos;
        rotation += vec2(-2*PI*delta.x()/width(),2*PI*delta.y()/height()); //TODO: warp
        rotation.y= clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
        lastPos = ev->pos();
        updateView();
#endif
        return true;
    }

    void updateView() {
        const Volume& source = current.volume;
        int2 size (source.x-2*source.marginX,source.y-2*source.marginY);
        while(2*size<displaySize) size *= 2;
        window.setSize(size);
        window.render();
    }

    void render(int2 position, int2) {
        Image image;
        const Volume& source = current.volume;
        assert( source );
        uint z = source.marginZ+(source.z-2*source.marginZ-1)*currentSlice;
        if(current.pass>=Threshold) image = squareRoot(source, z);
        else image = slice(source, z);
#if RENDER
        else {
            if(!renderVolume.data) {
                renderVolume = Volume(new uint16[source.x*source.y*source.z],source.x,source.y,source.z);
                //Volume smoothVolume = Volume(new uint16[source.x*source.y*source.z],source.x,source.y,source.z);
                //smooth<2>(smoothVolume, smoothVolume);
                //tile<2>(renderVolume, smoothVolume);
                //delete[] smoothVolume.data;
                clip(renderVolume);
            }
            mat3 view;
            view.rotateX(rotation.y); // pitch
            view.rotateZ(rotation.x); // yaw
#if PROFILE
            uint64 start = cpuTime();
            image = ::render(renderVolume, view);
            uint64 time = cpuTime()-start;
            static uint64 totalTime = 0; totalTime += time;
            static uint count = 0;  count++;
            qDebug() << tileSize << count << time / 1000.0 << "ms" << (totalTime / 1000.0 / count) <<"ms"<< (count / (totalTime / 1000000.0)) <<"fps";
            QTimer::singleShot(0, this, SLOT(update()));
#else
            image = ::render(renderVolume, view);
#endif
        }
#endif
        while(2*image.size()<displaySize) image=upsample(image);
        blit(position, image); //FIXME: direct slice->shm
    }

    // Settings
    static constexpr uint smoothFilterSize = 3; // Smooth pass averages samples in a (2×filterSize+1)³ window
    const Folder memoryFolder {"dev/shm"_}; // Should be a RAM (or local disk) filesystem large enough to hold up to 2 intermediate passes of volume data (up to 32bit per sample)
    const Folder resultFolder {"ptmp"_}; // Final results (histograms) are written there

    // Arguments
    Folder folder; // Contains source slice images
    string name; // Used to name intermediate and output files (folder base name)
    Pass force; // Starting from this pass, disables loading from disk cache

    // Variables
    struct PassData {
        Pass pass = Null;
        Map map;
        Volume volume;
    };
    PassData previous, current; // Processing alternates between each buffer (i.e double buffering)
    float currentSlice=0; // Normalized z coordinate of the currently shown slice
    Window window {this,int2(-1,-1),"Rock"_};

#if RENDER
    Volume renderVolume;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
#endif
} app( arguments()[0], toPass( arguments().size>1 ? arguments()[1] : "null" ) );
