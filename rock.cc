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

enum Pass { Invalid=-1, Source, Smooth, Threshold, Distance, Maximum, Undefined };
static constexpr uint passSampleSize[] = {2,2,4,4,4};
static ref<ref<byte>> passNames = {"source"_,"smooth"_,"threshold"_,"distance"_,"maximum"_};
const ref<byte>& str(Pass pass) { return passNames[(uint)pass]; }

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : ImageView {
    Rock(const ref<byte>& path, Pass force) : folder(path), name(section(path,'/',-2,-1)), force(force) {
        processVolume();
        window.show();
    }

    /// Clears any computed data
    void clear() {
        source = {}; target={};
        previous = current = Invalid;
        densityThreshold = 0;
    }

    /// Processes a volume
    void processVolume() {
        clear();
        assert(name);
        for(string& volume : temporaryFolder.list(Files)) {
            if(!startsWith(volume, name)) continue;
            ref<byte> passName = section("."_,-2,-2);
            Pass pass = (Pass)passNames.indexOf(passName);
            if(pass > Source && pass < force) {
                if(previous>=0) error(previous);
                previous=current; current=pass;
                mapVolume(pass);
                log("Found", passName);
                swap(source, target);
            }
        }
        setPass(force != Invalid ? force : Distance);
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
        volume.sampleSize = align(8, nextPowerOfTwo(log2(nextPowerOfTwo(volume.den/volume.num)))) / 8; // Computes the sample size necessary to represent 1 using the given scale factor
    }

    /// Loads source volume from all images (lexically ordered)
    bool mapVolume(Pass pass) {
        assert(!target.map && !target.volume);
        array<string> files = temporaryFolder.list(Files);
        if(pass < force) for(string& path : files) {
            if(!find(path,"."_+str(pass)+"."_)) continue;
            // Map target file in memory
            target.path = move(path);
            File file = File(target.path, temporaryFolder);
            parseVolumeMetadata(target.volume, target.path);
            target.map = Map(file, ReadWrite);
            target.volume.data = buffer<byte>(target.map);
            assert( target.volume );
            return true;
        }
        array<string> slices;
        if(pass==Source) {
            slices = folder.list(Files);
            const Image image = decodeImage(readFile(slices.first(), folder));
            target.volume.x = image.width, target.volume.y = image.height, target.volume.z = slices.size, target.volume.sampleSize=2, target.volume.den = 1<<12;
        }
        target.path = name+"."_+str(pass)+"."_+volumeMetadata(target.volume);
        log(target.path);
        File file (target.path, temporaryFolder, ReadWrite|Create);
        file.resize( (uint64)target.volume.sampleSize * target.volume.x * target.volume.y * target.volume.z );
        target.map = Map(file, ReadWrite);
        target.volume.data = buffer<byte>(target.map);
        assert( target.volume );
        if(pass==Source) {
            uint XY=target.volume.x*target.volume.y;
            uint16* const targetData = (Volume16&)target.volume;
            //#pragma omp parallel for
            for(uint z=0; z<slices.size; z++) {
                const Image image = decodeImage(Map(slices[z],folder)); //TODO: 16bit
                const byte4* const src = image.data;
                uint16* const dst = targetData + z*XY;
                for(uint i=0; i<XY; i++) dst[i] = src[i].b << 4; // Leaves 4bit headroom to convolve up to 16 samples without unpacking
            }
        }
        return false;
    }

    /// Executes pipeline until target pass
    void setPass(Pass targetPass) {
        if(targetPass==current) {}
        else if(targetPass==previous) swap(current,previous), swap(source, target); // Swaps current and previous pass
        else {
            if((current<previous) ^ (targetPass < current)) swap(current,previous), swap(source, target); // Restores current pass > previous pass
            if(targetPass<previous) clear(); // Reload volume when desired intermediate result is not available anymore
            while(current<targetPass) {
                previous = current;
                current = Pass(int(current)+1);
                if(current==Source) { mapVolume(Source); swap(source, target); }
                else {
                    Volume& target = this->target.volume;
                    Volume& source = this->source.volume;
                    target.x=source.x, target.y=source.y, target.z=source.z, target.marginX = source.marginX, target.marginY = source.marginY, target.marginZ = source.marginZ;
                    target.num=source.num, target.den=source.den, target.sampleSize=passSampleSize[current];
                    bool diskCache = (uint)current<(uint)passNames.indexOf(arguments().last());
                    if(!target && diskCache && mapVolume(current)) log(current,"from disk");
                    else {
                        if(!target.data) { // Disk cache was disabled
                            log(current,"in memory");
                            assert(!this->target.map && !this->target.path);
                            target.data = buffer<byte>(target.size());
                            assert(target.data);
                        }

                        log("Computing",current);
                        {Stopwatch stopwatch;

                            /* */ if(current==Smooth) {
                                smooth<smoothFilterSize>(target, source);
                            }
                            else if(current==Threshold) {
                                //if(!densityThreshold) densityThreshold = settings.value("densityThreshold", 0).toFloat(); FIXME
                                if(!densityThreshold) {
                                    log("Computing density threshold");
                                    densityThreshold = computeDensityThreshold(source);
                                    log("threshold =",densityThreshold,"=",densityThreshold*0x100);
                                    //settings.setValue("densityThreshold", densityThreshold); FIXME
                                }
                                threshold(target, source, densityThreshold);
                            }
                            else if(current==Distance) {
                                distance(target, source);
                            }
                            else if(current==Maximum) {
                                maximum(target, source);
                            }
                        }

                        if(diskCache) {
                            string path = name+"."_+str(current)+volumeMetadata(target);
                            assert(this->target.path != path);
                            log(this->target.path, "->", path);
                            rename(this->target.path, path); // Rename target file to the current pass
                            this->target.path = move(path);
                        }

                        if(current==Maximum) {
                            log("Computing pore size histogram");
                            writeFile(name+".histogram"_, str(histogram(target)), histogramFolder);
                        }
                    }
                    swap(this->source, this->target); // target pass becomes source pass for next iteration
                }
            }
        }
        if(target.volume.z && setSlice(currentSlice*source.volume.z/target.volume.z)) {}
        else updateView();
    }

    /// Shows an image corresponding to the volume slice at Z position \a index
    bool setSlice(uint index) {
        index = clip(0u, index, source.volume.z-source.volume.marginZ-1);
        if(currentSlice == index) return false;
        currentSlice = index;
        updateView();
        return true;
    }

    bool mouseEvent(int2 cursor, int2 size, Event, Button) {
        setSlice(source.volume.marginZ+(source.volume.z-source.volume.marginZ-1)*cursor.x/(size.x-1));
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
        if(currentSlice!=uint(-1)) {
            if(current==Distance) image = squareRoot(source.volume, currentSlice);
            else image = slice(source.volume, currentSlice);
        }
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
        window.setSize(image.size());
        window.render();
    }

    // Settings
    static constexpr uint smoothFilterSize = 2;
    const Folder temporaryFolder {"ptmp"_};
    const Folder histogramFolder {"ptmp"_};

    Folder folder;
    string name;
    struct { Map map; string path; Volume volume; } source, target;
    Volume renderVolume;

    Pass previous=Invalid;
    Pass current=Invalid;
    Pass force;
    uint currentSlice=0;
    float densityThreshold=0;

    Window window {this,int2(-1,-1),"Rock"_};

#if RENDER
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
#endif
} app( arguments().first(), (Pass)passNames.indexOf(arguments().last()) );
