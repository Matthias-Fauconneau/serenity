/// \file rock.cc Volume data processor
#include "thread.h"
#include "process.h"
#include "volume.h"
#include "volume-operation.h"
#include "time.h"
#include "window.h"
#include "display.h"
#include "render.h"
#include "png.h"

//#include "source.h"
//include "capsule.h"
//include "resample.h"
//#include "average.h"
//#include "median.h"
//#include "resample.h"
//#include "histogram.h"
//#include "threshold.h"
//#include "distance.h"
//#include "skeleton.h"
//#include "floodfill.h"
//#include "rasterize.h"
//#include "kernel-density-estimation.h"
//#include "analysis.h"
//#include "export.h"

String strByteCount(size_t byteCount) {
    if(byteCount < 1u<<10) return str(byteCount,"B"_);
    if(byteCount < 10u<<20) return str(byteCount>>10,"kiB"_);
    if(byteCount < 10u<<30) return str(byteCount>>20,"MiB"_);
    return str(byteCount>>30,"GiB"_);
}

struct GraphProcess : virtual Process {
    String id(const string& target, const Dict& arguments) {
        const Dict& localArguments = this->localArguments(target, arguments);
        return hex(ptr(&ruleForOutput(target)))+(localArguments?" "_+str(localArguments).slice(1,-1):String());
    }
    String label(const string& target, const Dict& arguments) {
        const Dict& localArguments = this->localArguments(target, arguments);
        return ruleForOutput(target).operation+(localArguments?" "_+str(localArguments).slice(1,-1):String());
    }
    String dot(array<String>& once, const Dict& arguments, const string& target) {
        String s;
        const Rule& rule = ruleForOutput(target);
        String targetResult = id(target, arguments);
        if(!once.contains(targetResult)) {
            once << copy(targetResult);
            if(&rule) {
                s <<'"'<<targetResult<<'"'<< "[shape=record, label=\""_<<label(target, arguments);
                for(string output: rule.outputs) s<<"|<"_<<output<<"> "_<<output;
                s<<"\"];\n"_;
                for(string input: rule.inputs) {
                    String inputResult = id(input, arguments);
                    s<<'"'<<inputResult<<"\":\""_<<input<<"\" -> \""_<<targetResult<<"\"\n"_;
                    s<<dot(once, arguments, input);
                };
            }
        }
        return s;
    }

    void generateSVG(const ref<string>& targets, const string& name, const string& folder){
        array<String> once;
        String s ("digraph \""_+name+"\" {\n"_);
        for(const string& target: targets) s << dot(once, arguments, target);
        s << "}"_;
        String path = "/dev/shm/"_+name+".dot"_;
        writeFile(path, s);
        ::execute("/ptmp/bin/dot"_,{path,"-Tsvg"_,"-o"_+folder+"/"_+name+".svg"_});
    }
};

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : virtual PersistentProcess, virtual GraphProcess, Widget {
    FILE(process) // Rock process definition (embedded in binary)
    Rock(const ref<string>& args) : PersistentProcess("rock"_) {
        specialParameters += "dump"_; specialParameters += "graph"_; specialParameters += "view"_; specialParameters += "cylinder"_; specialParameters += "storageFolder"_;
        String process;
        for(const string& arg: args) if(endsWith(arg, ".process"_)) { assert_(!process); process = readFile(arg,cwd); }
        array<string> targets = configure(args, process? : this->process());
        if(targetPaths.size>targets.size) error("Expected less names, skipped names"_, "["_+str(targetPaths.slice(targets.size))+"]"_, "using", map<string,string>(targetPaths.slice(0,targets.size), targets),
                                                  "\nHint: An unknown (mistyped?) target might be interpreted as target path"); //TODO: hint nearest (levenstein distance) target
        if(targets.size>targetPaths.size && arguments.value("view"_,"0"_)=="0"_) warn("Expected more names, skipped targets"_, targets.slice(targetPaths.size));
#ifndef BUILD
#define BUILD "undefined"
#endif
        if(arguments.contains("dump"_)) {
            log("Rock binary built on " __DATE__ " " __TIME__ " (" BUILD ")");
            log("Operations:",Interface<Operation>::factories.keys);
            log("Parameters:",parameters());
            log("Results:",resultNames);
            log("Targets:",targets);
            log("Arguments:",arguments);
            log("Target paths:",targetPaths);
        }
        if(arguments.contains("graph"_)) { generateSVG(targets, "process"_, getenv("HOME"_)); return; }
        if(!targets) {
            if(arguments) log("Arguments:",arguments);
            if(targetPaths) log("Target paths:",targetPaths);
            assert_(targets, "Expected target");
        }
        for(uint i: range(targets.size)) targetResults << getResult(targets[i], arguments);
        assert_(targetResults.size == targets.size, targets, targetResults);

        if(targetPaths.size>1 || (targetPaths.size==1 && !existsFolder(targetPaths[0],cwd))) { // Copies results to individually named files
            for(uint index: range(min(targetResults.size,targetPaths.size))) {
                const string& path = targetPaths[index];
                const shared<Result>& result = targetResults[index];
                String fileName = result->name+"."_+result->metadata;
                if(result->elements) {
                    assert_(existsFolder(path, cwd), path);
                    Folder folder(fileName, Folder(path, cwd), true);
                    for(const_pair<String,buffer<byte>> element: (const map<String,buffer<byte>>&)result->elements) writeFile(element.key+"."_+result->metadata, element.value, folder);
                } else {
                    assert_(result->data, fileName, result->elements);
                    String data = unsafeReference(result->data);
                    if(existsFolder(path, cwd)) {
                        Time time;
                        writeFile(fileName, data, Folder(path, cwd));
                        log(path+"/"_+fileName, "["_+strByteCount(data.size)+"]"_, time);
                    } else {
                        Time time;
                        writeFile(path, data, cwd);
                        log(fileName,"->",path, "["_+strByteCount(data.size)+"]"_, time);
                    }
                }
            }
        } else if(targetPaths.size == 1) { // Copies results into folder
            const string& path = targetPaths[0];
            if(!existsFolder(path,cwd)) {
                assert_(!existsFile(path,cwd), "New folder would overwrite existing file", path);
                Folder(path,cwd,true);
            } else if(Folder(path,cwd).list(Files).size<=12) {
                for(const String& file: Folder(path,cwd).list(Files)) ::remove(file, Folder(path,cwd)); // Cleanups previous results
            } else log("Skipped cleaning (Too many previous results)");
            for(const shared<Result>& result: targetResults) {
                Time time;
                String fileName = result->name+"{"_+toASCII(result->localArguments)+"}."_+result->metadata;
                //assert(result->data || result->elements, fileName);
                if(result->data) {
                    assert(!existsFolder(fileName), fileName);
                    writeFile(fileName, result->data, Folder(path, cwd));
                    log(fileName, "["_+strByteCount(result->data.size)+"]"_, time);
                } else if(result->elements) {
                    Folder folder(fileName, Folder(path, cwd), true);
                    for(const_pair<String,buffer<byte>> element: (const map<String,buffer<byte>>&)result->elements) writeFile(element.key+"."_+result->metadata, element.value, folder);
                }
            }
        } else assert(arguments.contains("view"_), "Expected target paths"_);

        // Displays target results
        if(arguments.value("view"_,"0"_)!="0"_) {
            for(const shared<Result>& result: targetResults) {
                if(result->data) {
                    if(result->metadata=="scalar"_) log_(str(result->name, "=", result->data));
                    else if(endsWith(result->metadata,"map"_) || endsWith(result->metadata,".tsv"_)) { // Distribution or scalar map
                        if(count(result->data,'\n')<16) log_(str(result->name, "["_+str(count(result->data,'\n'))+"]"_,":\n"_+result->data));
                    }
                    else if(inRange(1u,toVolume(result).sampleSize,4u)) { if(!current) current = share(result); } // Displays first displayable volume
                    else warn("Unknown format",result->metadata, result->name, result->localArguments);
                } else assert(result->elements, result->name);
            }
        }
        if(current) {
            window = unique<Window>(this,int2(-1,-1),"Rock"_);
            window->localShortcut(Escape).connect([]{exit();});
            window->clearBackground = false;
            updateView();
            window->show();
        }
    }

     void parseSpecialArguments(const ref<string>& specialArguments) override {
         targetPaths.clear();
        for(const string& argument: specialArguments) {
            /***/ if(endsWith(argument,".process"_)) {} // Already parsed extern process definition
            else if(existsFolder(argument,cwd) && !Folder(argument,cwd).list(Files|Folders|Hidden)) { removeFolder(argument,cwd); targetPaths << argument; } // Removes any empty target folder
            else if(!arguments.contains("path"_) && (existsFolder(argument,cwd) || existsFile(argument,cwd))) {
                if(existsFolder(argument,cwd)) for(const string& file: Folder(argument,cwd).list(Files|Folders)) {
                    assert_(!existsFolder(file,Folder(argument,cwd)), file, arguments);
                    assert_(imageFileFormat(Map(file,Folder(argument,cwd))), file, arguments);
                }
                arguments.insert(String("path"_), String(argument));
            }
            else if(!argument.contains('=')) targetPaths << argument;
            else error("Invalid argument", argument);
        }
    }

    bool mouseEvent(int2 cursor, int2 size, Event unused event, Button button) {
        if(button==WheelDown||button==WheelUp) {
            int volumeCount=0, volumeIndex=0;
            for(const shared<Result>& result: targetResults) if(toVolume(result)) { if(result==current) volumeIndex=volumeCount; volumeCount++; }
            int newVolumeIndex = clip<int>(0,volumeIndex+(button==WheelUp?1:-1),volumeCount-1);
            if(newVolumeIndex == volumeIndex) return true;
            volumeCount = 0;
            for(const shared<Result>& result: targetResults) if(toVolume(result)) { if(newVolumeIndex==volumeCount) current=share(result); volumeCount++; }
            updateView();
            return true;
        }
        if(!button) return false;
        /*if(renderVolume) {
            int2 delta = cursor-lastPos;
            lastPos = cursor;
            if(event != Motion) return false;
            rotation += vec2(-2*PI*delta.x/size.x,2*PI*delta.y/size.y);
            rotation.y= clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
        } else*/
        {
            float z = clip(0.f, float(cursor.x)/(size.x-1), 1.f);
            if(sliceZ != z) { sliceZ = z; updateView(); }
        }
        updateView();
        return true;
    }

    void updateView() {
        assert(current);
        Volume volume = toVolume(current);
        int2 size = volume.sampleCount.xy();
        while(2*size<displaySize) size *= 2;
        if(window->size != size) window->setSize(size);
        else window->render();
        window->setTitle(str(current));
    }

    void render(int2 position, int2 size) {
        assert(current);
        Volume volume = toVolume(current);
        if(volume.sampleSize==20) { exit(); return; } // Don't try to display ASCII
        if(volume.sampleSize>4) error(current->name, volume.sampleSize);
        if(renderVolume) {
            mat3 view;
            view.rotateX(rotation.y); // pitch
            view.rotateZ(rotation.x); // yaw
            shared<Result> empty = getResult("empty"_, arguments);
            shared<Result> density = getResult("density"_, arguments);
            shared<Result> intensity = getResult("intensity"_, arguments);
            Time time;
            assert_(position==int2(0) && size == framebuffer.size());
            ::render(framebuffer, toVolume(empty), toVolume(density), toVolume(intensity), view);
#if PROFILE
            log((uint64)time,"ms");
            window->render(); // Force continuous updates (even when nothing changed)
            wait.reset();
#endif
        } else
        {
            Image image = slice(volume, sliceZ, arguments.contains("cylinder"_));
            while(2*image.size()<=size) image=upsample(image);
            blit(position, image);
        }
    }

    const Folder& cwd = currentWorkingDirectory(); // Reference for relative paths
    array<string> targetPaths; // Path to file (or folders) where targets are copied
    array<shared<Result>> targetResults; // Generated datas for each target
    shared<Result> current;
    float sliceZ = 1./2; // Normalized z coordinate of the currently shown slice
    unique<Window> window = nullptr;

    bool renderVolume = false;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
} app ( arguments() );
