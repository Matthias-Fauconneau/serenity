/// \file build.cc Builds C++ projects with automatic module dependency resolution
#include "core/thread.h"
#include "core/data.h"
#include "core/string.h"
#include <unistd.h>

struct Module {
    string name;
    array<Module*> deps;
    Module(const ref<byte>& name):name(copy(name)){}
};
bool operator ==(const Module& a, const Module& b) { return a.name==b.name; }
bool operator ==(const Module& a, const ref<byte>& b) { return a.name==b; }
string str(const Module& o) { string s = copy(o.name); if(o.deps) s<<'{'<<str(o.deps)<<'}'; return s; }

struct Build {
    ref<byte> build = arguments()[0], target = arguments()[1], install = arguments().size>2?arguments()[2]:""_;
    const Folder& folder = currentWorkingDirectory();
    const ref<byte> tmp = "/dev/shm/"_;
    array<unique<Module>> modules;
    array<string> libraries;
    array<string> files;
    array<int> pids;

    string find(const ref<byte>& file, const ref<byte>& suffix=""_) {
        assert(file && !startsWith(file, "/"_), file);
        for(ref<byte> subfolder: folder.list(Folders|Recursive)) if(existsFile(file+suffix,Folder(subfolder, folder))) return subfolder+"/"_+file;
        return string();
    }

    /// Returns timestamp of the last modified interface header recursively parsing includes
    long parse(const ref<byte>& name) {
        File file (name+".h"_, folder);
        long lastEdit = file.modifiedTime();
        for(TextData s = file.read(file.size()); s; s.advance(1)) {
            if(s.match("#include \""_)) {
                ref<byte> name = s.until('.');
                string header = find(name,".h"_);
                if(header) lastEdit = max(lastEdit, parse(header));
            }
        }
        return lastEdit;
    }

    /// Compiles a module and its dependencies as needed
    /// \return Timestamp of the last modified module implementation (deep)
    long compile(const ref<byte>& target) {
        assert_(target);
        modules << unique<Module>(target);
        Module& parent = modules.last();
        File file (target+".cc"_, folder);
        long lastCompileEdit = file.modifiedTime(); // including headers
        long lastLinkEdit = lastCompileEdit; // including headers and their associated implementations (avoid getting all timestamps again before link)
        for(TextData s = file.read(file.size()); s; s.advance(1)) {
            if(s.match("#include "_)) {
                if(s.match('"')) { // module header
                    ref<byte> name = s.until('.');
                    assert_(name);
                    string header = find(name,".h"_);
                    if(header) lastCompileEdit = max(lastCompileEdit, parse(header));
                    string module = find(name,".cc"_);
                    if(!module || module == parent) continue;
                    if(!modules.contains(module)) lastLinkEdit = max(lastLinkEdit, max(lastCompileEdit, compile(module)));
                    else parent.deps << modules[modules.indexOf(module)].pointer;
                } else { // library header
                    for(;s.peek()!='\n';s.advance(1)) if(s.match("//"_)) { ref<byte> library=s.word(); if(library) libraries << string(library); break; }
                }
            }
            if(s.match("FILE("_) || s.match("ICON("_)) {
                ref<byte> file = s.word("_-"_);
                assert(file && !files.contains(file), file);
                files << string(file);
                s.skip(")"_);
            }
        }
        string object = tmp+build+"/"_+target+".o"_;
        if(!existsFile(object, folder) || lastCompileEdit >= File(object).modifiedTime()) {
            static const array<ref<byte>> flags = split("-c -pipe -std=c++11 -Wall -Wextra -I/ptmp/include -g -march=native -o"_);
            array<string> args;
            args << object << target+".cc"_;
            if(::find(build,"debug"_)) args << string("-DDEBUG"_);
            if(::find(build,"fast"_)) args <<  string("-Ofast"_);
            args << apply<string>(folder.list(Folders), [this](const string& subfolder){ return "-iquote"_+subfolder; });
            log(target);
            pids << execute("/ptmp/gcc-4.8.0/bin/g++"_,flags+toRefs(args), false); //TODO: limit to 8
        }
        return lastLinkEdit;
    }

    void fail() { log("Build failed"_); exit(-1); exit_thread(-1); }

    Build() {
        Folder(tmp+build, root(), true);
        for(ref<byte> subfolder: folder.list(Folders|Recursive)) Folder(tmp+build+"/"_+subfolder, root(), true);
        long lastEdit = compile( find(target,".cc"_) );
        //log(modules.first()); // Dependency tree
        bool fileChanged = false;
        if(files) {
            Folder(tmp+"files"_, root(), true);
            for(string& file: files) {
                file = find(replace(move(file),'_','/'));
                Folder subfolder = Folder(section(file,'/',0,-2), folder);
                ref<byte> name = section(file,'/',-2,-1);
                string object = tmp+"files/"_+name+".o"_;
                if(!existsFile(object) || File(name, subfolder).modifiedTime() >= File(object).modifiedTime()) {
                    if(execute("/usr/bin/ld"_,split("-r -b binary -o"_)<<object<<name, true, subfolder)) fail();
                    fileChanged = true;
                }
                file = move(object);
            }
        }
        string binary = tmp+build+"/"_+target+"."_+build;
        if(!existsFile(binary) || lastEdit >= File(binary).modifiedTime() || fileChanged) {
            array<string> args; args<<string("-o"_)<<binary;
            args << apply<string>(modules, [this](const unique<Module>& module){ return tmp+build+"/"_+module->name+".o"_; });
            args << files;
            args << string("-L/ptmp/lib"_) << apply<string>(libraries, [this](const string& library){ return "-l"_+library; });
            for(int pid: pids) if(wait(pid)) fail(); // Wait for each translation unit to finish compiling before final linking
            if(execute("/ptmp/gcc-4.8.0/bin/g++"_,toRefs(args))) fail();
            if(install) copy(install, target+"."_+build, root(), binary);
        }
    }
} build;
