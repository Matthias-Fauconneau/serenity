/// \file rock.cc Volume data processor
#include "thread.h"
#include "time.h"
#include "process.h"
#include "tool.h"

// Includes all operators used by rock process script
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

// Includes all available tools
//#include "view.h"
//#include "REV.h"

/// Command-line interface for rock volume data processing
struct Rock : virtual PersistentProcess {
    FILE(rock) // Rock process definition (embedded in binary)
    Rock(const ref<string>& args) : PersistentProcess("rock"_) {
        specialParameters += "dump"_; specialParameters += "view"_; specialTargets += "REV"_; specialParameters += "ViewREV"_; //FIXME
        String process;
        for(const string& arg: args) if(endsWith(arg, ".process"_)) { assert_(!process); process = readFile(arg,cwd); }
        array<string> targets = configure(args, process? : rock());
        if(targetPaths.size>targets.size) error("Expected less names, skipped names"_, "["_+str(targetPaths.slice(targets.size))+"]"_, "using", map<string,string>(targetPaths.slice(0,targets.size), targets),
                                                  "\nHint: An unknown (mistyped?) target might be interpreted as target path"); //TODO: hint nearest (levenstein distance) target
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
        for(string target: targets) targetResults << (specialTargets.contains(target) ? Interface<Tool>::instance(target)->execute(*this) : getResult(target, arguments));
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
                        log(path+"/"_+fileName, "["_+binaryPrefix(data.size)+"]"_, time);
                    } else {
                        Time time;
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
        } else assert(specialArguments.contains("view"_), "Expected target paths"_);

        for(string key: Interface<Tool>::factories.keys) if(specialArguments.contains(key) || specialArguments.contains(toLower(key))) {
            unique<Tool> tool = Interface<Tool>::instance(key);
            tool->execute(*this);
            tools << move(tool);
        }
    }

     void parseSpecialArguments(const ref<string>& specialArguments) override {
         targetPaths.clear();
        for(const string& argument: specialArguments) {
            /***/ if(endsWith(argument,".process"_)) {} // Already parsed extern process definition
            else if(existsFolder(argument,cwd) && !Folder(argument,cwd).list(Files|Folders|Hidden)) targetPaths << argument;
            else if(!arguments.contains("path"_) && (existsFolder(argument,cwd) || existsFile(argument,cwd))) {
                if(existsFolder(argument,cwd)) for(const string& file: Folder(argument,cwd).list(Files|Folders)) assert_(!existsFolder(file,Folder(argument,cwd)), file, arguments);
                arguments.insert(String("path"_), String(argument));
            }
            else if(!argument.contains('=')) targetPaths << argument;
            else error("Invalid argument", argument);
        }
    }

    const Folder& cwd = currentWorkingDirectory(); // Reference for relative paths
    array<string> targetPaths; // Path to file (or folders) where targets are copied
    array<unique<Tool>> tools;
} app ( arguments() );
