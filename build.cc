#include "thread.h"
#include "data.h"
#include "string.h"

struct Module {
    string name;
    array<Module*> deps;
    Module(const ref<byte>& name):name(string(name)){}
};
bool operator ==(const Module& a, const Module& b) { return a.name==b.name; }
bool operator ==(const Module& a, const ref<byte>& b) { return a.name==b; }
string str(const Module& o) { string s = copy(o.name); if(o.deps) s<<'{'<<str(o.deps)<<'}'; return s; }

struct Build {
    ref<byte> build = arguments()[0], target = arguments()[1];
    const Folder& folder = currentWorkingDirectory();
    array<unique<Module>> modules;

    /// Compiles a module and its dependencies as needed
    /// \return Timestamp of the last edit (deep)
    long compile(ref<byte> target) {
        modules << unique<Module>(target);
        Module& parent = modules.last();
        File file (target+".cc"_, folder);
        long lastEdit = file.modifiedTime();
        for(TextData s = file.read(file.size()); s; s.line()) {
            if(s.match("#include \""_)) {
                ref<byte> module = s.until('.');
                lastEdit = max(lastEdit, File(module+".h"_, folder).modifiedTime());
                if(module == parent) continue;
                if(!modules.contains(module) && existsFile(module+".cc"_, folder)) lastEdit = max(lastEdit, compile(module));
                if(modules.contains(module)) parent.deps << modules.find(module)->pointer;
            }
        }
        string object = build+"/"_+target+".o"_;
        if(!existsFile(object, folder) || lastEdit >= File(object, folder).modifiedTime()) {
            static const auto flags = toStrings(split("-I/ptmp/include -pipe -march=native -std=c++11 -funsigned-char -fno-exceptions -Wall -Wextra -Wno-missing-field-initializers -c -o"_));
            array<string> args = copy(flags); args<<object<<target+".cc"_;
            log(target);
            if(execute("/ptmp/gcc-4.8.0/bin/g++"_,args)) { log("Build failed"_); exit(-1); exit_thread(-1); }
        }
        return lastEdit;
    }
    Build() {
        //FIXME: build self update
        string binary = build+"/"_+target;
        long lastEdit = compile(target);
        //log(modules.first()); // Dependency tree
        if(lastEdit >= File(binary, folder).modifiedTime()) {
            static const auto flags = toStrings(split("-L/ptmp/lib -o"_));
            array<string> args = copy(flags); args<<string(target)<<apply<string>(modules,[](const unique<Module>& o){return o->name+".o"_;});
            log(binary, args);
            if(execute("/ptmp/gcc-4.8.0/bin/g++"_,args)) { exit(-1); exit_thread(-1); }
        }
    }
} build;
