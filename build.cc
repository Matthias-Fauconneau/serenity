#include "thread.h"
#include "data.h"
#include "string.h"
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
    ref<byte> build = arguments()[0], target = arguments()[1];
    const Folder& folder = currentWorkingDirectory();
    const ref<byte> tmp = "/dev/shm/"_;
    array<unique<Module>> modules;
    array<string> libraries;
    array<string> files;
    array<int> pids;

    /// Returns timestamp of the last modified interface header recursively parsing includes
    long parse(ref<byte> target) {
        File file (target+".h"_, folder);
        long lastEdit = file.modifiedTime();
        for(TextData s = file.read(file.size()); s; s.advance(1)) {
            if(s.match("#include \""_)) {
                ref<byte> header = s.until('.');
                if(existsFile(header+".h"_, folder)) lastEdit = max(lastEdit, parse(header));
            }
        }
        return lastEdit;
    }

    /// Compiles a module and its dependencies as needed
    /// \return Timestamp of the last modified module implementation (deep)
    long compile(ref<byte> target) {
        modules << unique<Module>(target);
        Module& parent = modules.last();
        File file (target+".cc"_, folder);
        long lastCompileEdit = file.modifiedTime(); // including headers
        long lastLinkEdit = lastCompileEdit; // including headers and their associated implementations (avoid getting all timestamps again before link)
        for(TextData s = file.read(file.size()); s; s.advance(1)) {
            if(s.match("#include "_)) {
                if(s.match('"')) { // module header
                    ref<byte> module = s.until('.');
                    assert_(module);
                    if(existsFile(module+".h"_, folder)) lastCompileEdit = max(lastCompileEdit, parse(module));
                    if(module == parent) continue;
                    if(!modules.contains(module) && existsFile(module+".cc"_, folder)) lastLinkEdit = max(lastLinkEdit, max(lastCompileEdit, compile(module)));
                    if(modules.contains(module)) parent.deps << modules[modules.indexOf(module)].pointer;
                } else { // library header
                    for(;s.peek()!='\n';s.advance(1)) if(s.match("//"_)) { ref<byte> library=s.word(); if(library) libraries << string(library); break; }
                }
            }
            if(s.match("FILE("_) || s.match("ICON("_)) {
                ref<byte> file = s.word();
                if(file) files << string(file);
                s.skip(")"_);
            }
        }
        string object = tmp+build+"/"_+target+".o"_;
        if(!existsFile(object, folder) || lastCompileEdit >= File(object).modifiedTime()) {
            static const array<ref<byte>> flags = split("-I/ptmp/include -pipe -march=native -std=c++11 -Wall -Wextra -c -g -o"_); //-funsigned-char -fno-exceptions -Wno-missing-field-initializers
            array<string> args;
            args << object << target+".cc"_;
            if(find(build,"debug"_)) args << string("-DDEBUG"_);
            if(find(build,"fast"_)) args <<  string("-Ofast"_);
            log(target);
            pids << execute("/ptmp/gcc-4.8.0/bin/g++"_,flags+toRefs(args), false); //TODO: limit to 8
        }
        return lastLinkEdit;
    }

    void fail() { log("Build failed"_); exit(-1); exit_thread(-1); }

    Build() {
        Folder(tmp+build, root(), true);
        long lastEdit = compile(target);
        //log(modules.first()); // Dependency tree
        bool fileChanged = false;
        if(files) {
            Folder(tmp+"files"_,root(),true);
            chdir("files"); // Avoids 'files_' prefix in embedded file symbol name
            for(const string& file: files) {
                string object = tmp+"files/"_+file+".o"_;
                if(!existsFile(object, folder) || File(file, folder).modifiedTime() >= File(object, folder).modifiedTime()) {
                    if(execute("/usr/bin/ld"_,split("-r -b binary -o"_)<<object<<file)) fail();
                    fileChanged = true;
                }
            }
            chdir("..");
        }
        string binary = tmp+build+"/"_+target;
        if(!existsFile(binary, folder) || lastEdit >= File(binary, folder).modifiedTime() || fileChanged) {
            array<string> args; args<<string("-o"_)<<binary;
            args << apply<string>(modules, [this](const unique<Module>& module){ return tmp+build+"/"_+module->name+".o"_; });
            args << apply<string>(files, [this](const string& file){ return tmp+"files/"_+file+".o"_; });
            args << string("-L/ptmp/lib"_) << apply<string>(libraries, [this](const string& library){ return "-l"_+library; });
            for(int pid: pids) if(wait(pid)) fail(); // Wait for each translation unit to finish compiling before final linking
            if(execute("/ptmp/gcc-4.8.0/bin/g++"_,toRefs(args))) fail();
        }
    }
} build;
