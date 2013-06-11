/// \file build.cc Builds C++ projects with automatic module dependency resolution
#include "thread.h"
#include "data.h"
#include "string.h"

struct Node {
    string name;
    array<Node*> children;
    Node(const ref<byte>& name):name(copy(name)){}
};
bool operator ==(const Node& a, const Node& b) { return a.name==b.name; }
bool operator ==(const Node& a, const ref<byte>& b) { return a.name==b; }
string str(const Node& o) { string s = copy(o.name); if(o.children) s<<'{'<<str(o.children)<<'}'; return s; }

string dot(array<const Node*>& once, const Node& o) {
    string s;
    if(!once.contains(&o)) {
        once << &o;
        s<<'"'<<o.name<<'"'<<"[shape=record];\n"_;
        for(Node* dep: o.children) { s<<'"'<<o.name<<"\" -> \""_<<dep->name<<"\"\n"_<<dot(once, *dep); }
    }
    return s;
}

void generateSVG(const Node& root, const ref<byte>& folder){
    ref<byte> name = root.name.contains('/') ? section(root.name,'/',-2,-1) : ref<byte>(root.name);
    string s ("digraph \""_+name+"\" {\n"_);
    array<const Node*> once;
    s << ::dot(once, root);
    s << "}"_;
    string path = "/dev/shm/"_+name+".dot"_;
    writeFile(path, s);
    ::execute("/ptmp/bin/dot"_,{path,"-Tsvg"_,"-o"_+folder+"/"_+name+".svg"_});
}

struct Build {
    ref<byte> build = arguments()[0], target = arguments()[1], install = arguments().size>2?arguments()[2]:""_;
    bool graph = build=="graph"_, compile = !graph;
    const Folder& folder = currentWorkingDirectory();
    const ref<byte> tmp = "/dev/shm/"_;
    array<unique<Node>> modules;
    array<string> libraries;
    array<string> files;
    array<int> pids;

    array<string> sources = folder.list(Files|Recursive);
    /// Returns the first path matching file
    string find(const ref<byte>& file) { for(string& path: sources) if(endsWith(path, file)) return string(path.contains('.')?section(path,'.',0,-2):path); return string(); }

    /// Returns timestamp of the last modified interface header recursively parsing includes
    long parseHeader(const ref<byte>& name) {
        File file (name+".h"_, folder);
        long lastEdit = file.modifiedTime();
        for(TextData s = file.read(file.size()); s; s.advance(1)) {
            if(s.match("#include \""_)) {
                ref<byte> name = s.until('.');
                string header = find(name+".h"_);
                if(header) lastEdit = max(lastEdit, parseHeader(header));
            }
        }
        return lastEdit;
    }

    /// Compiles a module and its dependencies as needed
    /// \return Timestamp of the last modified module implementation (deep)
    long processModule(const ref<byte>& target) {
        assert_(target);
        modules << unique<Node>(target);
        Node& parent = modules.last();
        File file (target+".cc"_, folder);
        long lastCompileEdit = file.modifiedTime(); // including headers
        long lastLinkEdit = lastCompileEdit; // including headers and their associated implementations (avoid getting all timestamps again before link)
        for(TextData s = file.read(file.size()); s; s.advance(1)) {
            if(s.match("#include "_)) {
                if(s.match('"')) { // module header
                    ref<byte> name = s.until('.');
                    assert_(name);
                    string header = find(name+".h"_);
                    if(header) lastCompileEdit = max(lastCompileEdit, parseHeader(header));
                    string module = find(name+".cc"_);
                    if(!module || module == parent) continue;
                    if(!modules.contains(module)) lastLinkEdit = max(lastLinkEdit, max(lastCompileEdit, processModule(module)));
                    parent.children << modules[modules.indexOf(module)].pointer;
                } else { // library header
                    for(;s.peek()!='\n';s.advance(1)) if(s.match("//"_)) { ref<byte> library=s.word(); if(library) libraries << string(library); break; }
                }
            }
            if(s.match("FILE("_) || s.match("ICON("_)) {
                ref<byte> file = s.word("_-"_);
                assert_(file && !files.contains(file), file);
                files << string(file);
                s.skip(")"_);
            }
        }
        if(compile) {
            string object = tmp+build+"/"_+target+".o"_;
            if(!existsFile(object, folder) || lastCompileEdit >= File(object).modifiedTime()) {
                static const array<ref<byte>> flags = split("-c -pipe -std=c++11 -Wall -Wextra -I/ptmp/include -march=native -o"_);
                array<string> args;
                args << object << target+".cc"_;
                if(::find(build,"debug"_)) args << string("-g"_) << string("-Og"_) << string("-DNO_INLINE"_) << string("-DASSERT"_);
                else if(::find(build,"fast"_)) args << string("-g"_) << string("-Ofast"_);
                else if(::find(build,"release"_)) args <<  string("-Ofast"_);
                else error("Unknown build",build);
                args << apply<string>(folder.list(Folders), [this](const string& subfolder){ return "-iquote"_+subfolder; });
                log(target);
                pids << execute("/ptmp/gcc-4.8.0/bin/g++"_,flags+toRefs(args), false); //TODO: limit to 8
            }
        }
        return lastLinkEdit;
    }

    void fail() { log("Build failed"_); exit(-1); exit_thread(-1); }

    Build() {
        Folder(tmp+build, root(), true);
        for(ref<byte> subfolder: folder.list(Folders|Recursive)) Folder(tmp+build+"/"_+subfolder, root(), true);
        long lastEdit = processModule( find(target+".cc"_) );
        if(compile) {
            bool fileChanged = false;
            if(files) {
                Folder(tmp+"files"_, root(), true);
                for(string& file: files) {
                    string path = find(replace(file,"_"_,"/"_));
                    assert_(path, file);
                    Folder subfolder = Folder(section(path,'/',0,-2), folder);
                    ref<byte> name = section(path,'/',-2,-1);
                    string object = tmp+"files/"_+name+".o"_;
                    if(!existsFile(object) || File(name, subfolder).modifiedTime() >= File(object).modifiedTime()) {
                        if(execute("/usr/bin/ld"_,split("-r -b binary -o"_)<<object<<name, true, subfolder)) fail();
                        fileChanged = true;
                    }
                    file = move(object);
                }
            }
            string name = target+"."_+build;
            string binary = tmp+build+"/"_+name;
            if(!existsFile(binary) || lastEdit >= File(binary).modifiedTime() || fileChanged) {
                array<string> args; args<<string("-o"_)<<binary;
                args << apply<string>(modules, [this](const unique<Node>& module){ return tmp+build+"/"_+module->name+".o"_; });
                args << files;
                args << string("-L/ptmp/lib"_) << apply<string>(libraries, [this](const string& library){ return "-l"_+library; });
                for(int pid: pids) if(wait(pid)) fail(); // Wait for each translation unit to finish compiling before final linking
                if(execute("/ptmp/gcc-4.8.0/bin/g++"_,toRefs(args))) fail();
            }
            if(install && (!existsFile(name, install) || File(binary).modifiedTime() > File(name, install).modifiedTime())) copy(root(), binary, install, name);
        }
        if(graph) generateSVG(modules.first(), getenv("HOME"_));
    }
} build;
