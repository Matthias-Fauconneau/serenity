#include "process.h"
#include "data.h"
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

enum Pass { Null, Source, Smooth, Threshold, Distance, Maximum, Undefined };
static constexpr uint passSampleSize[] = {0,2,2,4,4,4};
static ref<ref<byte>> passNames = {"null"_, "source"_,"smooth"_,"threshold"_,"distance"_,"maximum"_};
const ref<byte>& str(Pass pass) { return passNames[(uint)pass]; }

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : Widget {
    Rock(const ref<byte>& path, Pass force) : folder(path), name(section(path,'/',-2,-1)), force(force) {
        window.localShortcut(Escape).connect(&exit);
        window.clearBackground = false;
        processVolume();
        window.show();
    }

    /// Clears any computed data
    void clear() {
        source = {}; target={};
        previous = current = Null;
        densityThreshold = 0;
    }

    /// Processes a volume
    void processVolume() {
        clear();
        assert(name);
        for(string& volume : temporaryFolder.list(Files)) {
            if(find(volume, ".swap."_)) { remove(volume, temporaryFolder); continue; }
            if(!startsWith(volume, name)) continue;
            ref<byte> passName = section(volume,'.',-3,-2);
            Pass pass = (Pass)passNames.indexOf(passName);
            if(pass > Source && (force==Null || pass < force)) {
                if(previous>Null) error("Too many intermediate passes stored",previous);
                previous=current; current=pass;
                mapVolume(pass);
                swap(source, target);
            }
        }
        setPass(force != Null ? force : Distance);
    }

    /// Serializes volume metadata (sample data format)
    string volumeMetadata(const Volume& volume) {
        string s; s << str(volume.x) << 'x' << str(volume.y) << 'x' << str(volume.z);
        if(volume.marginX||volume.marginY||volume.marginZ) s << '+' << str(volume.marginX) << '+' << str(volume.marginY) << '+' << str(volume.marginZ);
        s << '-' << hex(volume.num) << ':' << hex(volume.den);
        if(volume.offsetX||volume.offsetY||volume.offsetZ) s << "-tiled"_;
        return s;
    }

    /// Parses volume metadata (sample format)
    void parseVolumeMetadata(Volume& volume, const ref<byte>& path) { //FIXME: use parser
        TextData s ( section(path,'.',-2,-1) );
        volume.x = s.integer(); s.skip("x"_);
        volume.y = s.integer(); s.skip("x"_);
        volume.z = s.integer();
        if(s.match('+')) {
            volume.marginX = s.integer(); s.skip("+"_);
            volume.marginY = s.integer(); s.skip("+"_);
            volume.marginZ = s.integer();
        }
        s.skip("-"_);
        volume.num = s.hexadecimal(); s.skip(":"_);
        volume.den = s.hexadecimal();
        assert(volume.num && volume.den, path, volume.num, volume.den);
        volume.sampleSize = align(8, nextPowerOfTwo(log2(nextPowerOfTwo((volume.den+1)/volume.num)))) / 8; // Computes the sample size necessary to represent 1 using the given scale factor
    }

    /// Loads source volume from all images (lexically ordered)
    bool mapVolume(Pass pass) {
        assert(!target.map && !target.volume);
        array<string> files = temporaryFolder.list(Files);
        for(string& path : files) {
            if(!find(path,"."_+str(pass)+"."_)) continue;
            // Map target file in memory
            target.path = move(path);
            File file = File(target.path, temporaryFolder, ReadWrite);
            parseVolumeMetadata(target.volume, target.path);
            target.map = Map(file, Map::Prot(Map::Read|Map::Write));
            target.volume.data = buffer<byte>(target.map);
            assert( target.volume );
            log("Loading",target.path);
            return (force == Null || pass < force);
        }
        array<string> slices;
        if(pass==Source) {
            slices = folder.list(Files);
            Map file (slices.first(), folder);
            const Tiff16 image (file);
            target.volume.x = image.width, target.volume.y = image.height, target.volume.z = slices.size, target.volume.sampleSize=2, target.volume.den = (1<<16)-1;
        }
        target.path = name+".swap."_+volumeMetadata(target.volume);
        File file (target.path, temporaryFolder, Flags(ReadWrite|Create));
        file.resize( target.volume.size() * target.volume.sampleSize );
        target.map = Map(file, Map::Prot(Map::Read|Map::Write));
        target.volume.data = buffer<byte>(target.map);
        assert( target.volume.data.size == target.volume.size()*target.volume.sampleSize, target.volume.data.size/1024/1024, target.volume);
        if(pass==Source) {
            uint XY=target.volume.x*target.volume.y;
            uint16* const targetData = (Volume16&)target.volume;
            Time time;
            for(uint z=0; z<slices.size; z++) Tiff16(Map(slices[z],folder)).read(targetData+z*XY);
            log(current, time);
        }
        return false;
    }

    /// Executes pipeline until target pass
    void setPass(Pass targetPass) {
        assert(targetPass > Null);
        if(targetPass==current) {}
        else if(targetPass==previous) swap(current,previous), swap(source, target); // Swaps current and previous pass
        else {
            if((current<previous) ^ (targetPass < current)) swap(current,previous), swap(source, target); // Restores current pass > previous pass
            if(targetPass<previous) clear(); // Reload volume when desired intermediate result is not available anymore
            while(current<targetPass) {
                previous = current;
                current = Pass(int(current)+1);
                if(current==Source) {
                    mapVolume(Source);
                    string path = name+".source."_+volumeMetadata(target.volume);
                    if(this->target.path != path) { rename(this->target.path, path, temporaryFolder); this->target.path=move(path); }
                    swap(source, target);
                } else {
                    Volume& target = this->target.volume;
                    Volume& source = this->source.volume;
                    if(target.sampleSize != passSampleSize[current]) this->target = {}; // memory map size mismatch
                    target.x=source.x, target.y=source.y, target.z=source.z, target.marginX = source.marginX, target.marginY = source.marginY, target.marginZ = source.marginZ;
                    target.num=source.num, target.den=source.den, target.sampleSize=passSampleSize[current];
                    if(!target && (force == Null || current < force) && mapVolume(current)) {}
                    else {
                        if(this->target.path) { // Renames target file before processing (invalidate file while processing)
                            string path = name+".swap."_+volumeMetadata(target);
                            if(this->target.path != path) { // If not created on this pass
                                rename(this->target.path, path, temporaryFolder); // Rename target file to the current pass
                                this->target.path = move(path);
                            }
                        } else { // Allocate target on heap when disk cache is disabled
                            log("Computing",current,"into",target.size()*target.sampleSize/1024/1024,"MB RAM (Disk cache disabled by user)");
                            this->target.map = Map();
                            this->target.path.clear();
                            target.data = buffer<byte>(target.size()*target.sampleSize);
                            assert(target.data);
                        }

                        Time time;
                        /* */ if(current==Smooth) {
                            smooth<smoothFilterSize>(target, source);
                        }
                        else if(current==Threshold) {
                            if(1 || !existsFile(name+".density"_, histogramFolder)) {
                                Time time;
                                writeFile(name+".density"_, str(histogram(source)), histogramFolder);
                                log("density", time);
                            }
                            Histogram density = parseHistogram( readFile(name+".density"_, histogramFolder) );
                            // Computes density threshold as local (around 0.45=115) minimum of density histogram.
                            uint minimum = -1;
                            for(uint i=115-9; i<=115+9; i++) { //FIXME: find local minimum without these bounds
                                if(density.count[i] < minimum) {
                                    densityThreshold = float(i) / density.binCount;
                                    minimum = density.count[i];
                                }
                            }
                            log("Using density threshold =",densityThreshold,"("_+str(int(0x100*densityThreshold))+")"_);
                            threshold(target, source, densityThreshold);
                        }
                        else if(current==Distance) {
                            distance(target, source);
                            if(target.den/target.num<(1<<16)) { // Packs volume if possible (in-place)
                                const Volume32& target32 = target;
                                target.sampleSize = 2;
                                Time time;
                                pack(target, target32);
                                log("pack", time);
                                if(this->target.path) File(this->target.path, temporaryFolder).resize( target.size() * target.sampleSize );
                            }
                        }
                        else if(current==Maximum) {
                            maximum(target, source);
                        }
                        log(current, time);

                        if(this->target.path) { // Renames target file to its real name once data is valid
                            string path = name+"."_+str(current)+"."_+volumeMetadata(target);
                            assert(this->target.path != path);
                            rename(this->target.path, path, temporaryFolder); // Rename target file to the current pass
                            this->target.path = move(path);
                        }

                        if(current==Maximum) {
                            log("Computing pore size histogram");
                            writeFile(name+".radius"_, str(histogram(target)), histogramFolder);
                        }
                    }
                    swap(this->source, this->target); // target pass becomes source pass for next iteration
                }
            }
        }
        updateView();
    }

    /// Shows an image corresponding to the volume slice at Z position \a index
    void setSlice(float slice) {
        slice = clip(0.f, slice, 1.f);
        if(currentSlice != slice) { currentSlice = slice; updateView(); }
    }

    bool mouseEvent(int2 cursor, int2 size, Event, Button button) {
        if(!window.state)
        if(button==WheelDown) setPass((Pass)max((int)Source,current-1));
        if(button==WheelUp) setPass((Pass)min((int)Distance,current+1));
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
        int2 size (source.volume.x-2*source.volume.marginX,source.volume.y-2*source.volume.marginY);
        while(2*size<displaySize) size *= 2;
        window.setSize(size);
        window.render();
    }

    void render(int2 position, int2) {
        Image image; //FIXME: unnecessary copy
        assert( source.volume );
        uint z = source.volume.marginZ+(source.volume.z-2*source.volume.marginZ-1)*currentSlice;
        if(current==Distance) image = squareRoot(source.volume, z);
        else image = slice(source.volume, z);
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
        blit(position, image);
    }

    // Settings
    static constexpr uint smoothFilterSize = 2;
    const Folder temporaryFolder {"ptmp"_};
    const Folder histogramFolder {"ptmp"_};

    Folder folder;
    string name;
    struct { Map map; string path; Volume volume; } source, target;
    Volume renderVolume;

    Pass previous=Null;
    Pass current=Null;
    Pass force;
    float currentSlice=0;
    float densityThreshold=0;

    Window window {this,int2(-1,-1),"Rock"_};

#if RENDER
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
#endif
} app( arguments().first(), (Pass)max(0,passNames.indexOf(arguments().last())) );
