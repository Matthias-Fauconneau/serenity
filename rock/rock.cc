/// \file rock.cc Volume data processor
#include "thread.h"
#include "time.h"
#include "process.h"
#include "view.h"
#include "window.h"
#include "interface.h"
#include "png.h"

// Includes all operators, tools and views
//#include "source.h"
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
//#include "export.h"
//#include "slice.h"
//#include "plot.h"
//#include "REV.h"
//#include "prune.h"
//include "summary.h

/// Command-line interface for rock volume data processing
struct Rock : virtual PersistentProcess {
    FILE(rock) // Rock process definition (embedded in binary)
    Rock(const ref<string>& args) : PersistentProcess("rock"_) {
        specialParameters += "dump"_; specialParameters += "view"_;
        String process;
        for(const string& arg: args) if(endsWith(arg, ".process"_)) { assert_(!process); process = readFile(arg,cwd); }
        array<string> targets = configure(args, process? : rock());
        if(targetPaths.size>targets.size) error("Expected less names, skipped names"_, "["_+str(targetPaths.slice(targets.size))+"]"_, "using", map<string,string>(targetPaths.slice(0,targets.size), targets),
                                                "\nHint: An unknown (mistyped?) target might be interpreted as target path", rules,'\n'); //TODO: hint nearest (levenstein distance) target
        if(targets.size>targetPaths.size && (targetPaths.size!=1 || !existsFolder(targetPaths[0],cwd)) && specialArguments.value("view"_,"0"_)=="0"_)
            warn("Expected more names, skipped targets"_, targets.slice(targetPaths.size));
#ifndef BUILD
#define BUILD "undefined"
#endif
        if(specialArguments.contains("dump"_)) {
            log("Binary built on " __DATE__ " " __TIME__ " (" BUILD ")");
            log("Tools:",Interface<Tool>::factories.keys);
            log("Operations:",Interface<Operation>::factories.keys);
            log("Parameters:",parameters());
            log("Results:",resultNames);
            log("Targets:",targets);
            log("Arguments:",arguments);
            log("Target paths:",targetPaths);
        }
        if(!targets && !specialArguments) {
            if(arguments) log("Arguments:",arguments);
            if(targetPaths) log("Target paths:",targetPaths);
            assert_(targets, "Expected target");
        }
        for(string target: targets) targetResults << getResult(target, arguments);
        assert_(targetResults.size == targets.size, targets, targetResults);

        if(targetPaths.size>1 || (targetPaths.size==1 && (!existsFolder(targetPaths[0],cwd) || targetResults[0]->elements))) { // Copies results to individually named files
            for(uint index: range(min(targetResults.size,targetPaths.size))) {
                const string& path = targetPaths[index];
                const shared<Result>& result = targetResults[index];
                String fileName = result->name+"."_+result->metadata;
                if(result->elements) {
                    Folder folder(path, cwd, true);
                    for(const_pair<String,buffer<byte>> element: (const map<String,buffer<byte>>&)result->elements) writeFile(element.key+"."_+result->metadata, element.value, folder);
                } else {
                    String data = unsafeReference(result->data);
                    if(existsFolder(path, cwd)) {
                        Time time;
                        writeFile(fileName, data, Folder(path, cwd));
                        log(path+"/"_+fileName, "["_+binaryPrefix(data.size)+"]"_, time);
                    } else {
                        Time time;
                        assert_(!(existsFile(path, cwd) && File(path, cwd).size() >= 1<<30 && data.size <= 1<<20), "Would override large existing result, need to be removed manually", path);
                        writeFile(path, data, cwd);
                        log(fileName,"->",path, "["_+binaryPrefix(data.size)+"]"_, time);
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
                String fileName = result->name+(result->relevantArguments?"{"_+toASCII(result->relevantArguments)+"}"_:""_)+"."_+result->metadata;
                //assert(result->data || result->elements, fileName);
                if(result->data) {
                    assert(!existsFolder(fileName), fileName);
                    writeFile(fileName, result->data, Folder(path, cwd));
                    log(fileName, "["_+binaryPrefix(result->data.size)+"]"_, time);
                } else if(result->elements) {
                    Folder folder(fileName, Folder(path, cwd), true);
                    for(const_pair<String,buffer<byte>> element: (const map<String,buffer<byte>>&)result->elements) writeFile(element.key+"."_+result->metadata, element.value, folder);
                }
            }
        } else assert(specialArguments.value("view"_,"0"_)!="0"_, "Expected target paths"_);

        if(specialArguments.value("view"_,"0"_)!="0"_) {
            for(const shared<Result>& result: targetResults) {
                if(result->data) view(result->metadata, result->name, result->data);
                for(auto data: result->elements) view(result->metadata, data.key, data.value);
            }
        }

        if(viewers) {
            title = String("Rock"_); //join(apply(targetResults,[](const shared<Result>& r){return copy(r->name);}),", "_);
            window = unique<Window>(&grids, int2(0,0), title);
            window->localShortcut(Escape).connect([]{exit();});
            window->localShortcut(PrintScreen).connect([this]{
                assert_(!framebuffer && !clipStack);
                framebuffer = Image(1920,1080); //Image(724,1024);
                currentClip = Rect(framebuffer.size());
                fill(Rect(framebuffer.size()),1);
                grids.render(0,framebuffer.size());
                writeFile(title+".png"_, encodePNG(framebuffer), home());
                framebuffer = Image();
            });
            window->backgroundColor = 1;

            for(array<unique<View>>& views : viewers.values) {
                WidgetGrid grid;
                for(unique<View>& view: views) grid << view.pointer;
                grids << move(grid);
            }
            window->show();
        }
    }

    bool view(const string& metadata, const string& name, const buffer<byte>& data) {
        for(array<unique<View>>& views: viewers.values) for(unique<View>& view: views) if( view->view(metadata, name, data) ) return true; // Tries to append to existing view first
        for(auto viewer: Interface<View>::factories) {
            unique<View> view  = viewer.value->constructNewInstance();
            if( view->view(metadata, name, data) ) { viewers[viewer.key] << move(view); return true; }
        }
        warn("Unknown format",metadata, name);
        return false;
    }

     void parseSpecialArguments(const ref<string>& specialArguments) override {
         targetPaths.clear();
        for(const string& argument: specialArguments) {
            /***/ if(endsWith(argument,".process"_)) {} // Already parsed extern process definition
            else if(existsFolder(argument,cwd) && !Folder(argument,cwd).list(Files|Folders|Hidden)) targetPaths << argument;
            else if(!arguments.contains("path"_) && (existsFolder(argument,cwd) || existsFile(argument,cwd))) {
                if(existsFolder(argument,cwd)) for(const string& file: Folder(argument,cwd).list(Files|Folders)) assert_(!existsFolder(file,Folder(argument,cwd)), file, arguments);
                else assert_(File(argument,cwd).size()>=1<<20);
                arguments.insert(String("path"_), String(argument));
            }
            else if(!argument.contains('=')) targetPaths << argument;
            else error("Invalid argument", argument);
        }
    }

    const Folder& cwd = currentWorkingDirectory(); // Reference for relative paths
    array<string> targetPaths; // Path to file (or folders) where targets are copied
    array<shared<Result>> targetResults; // Generated datas for each target

    map<string, array<unique<View>>> viewers;
    String title;
    unique<Window> window = nullptr;
    VList<WidgetGrid> grids;
} app ( arguments() );
