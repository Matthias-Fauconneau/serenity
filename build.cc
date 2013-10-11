/// \file build.cc Builds C++ projects with automatic module dependency resolution
#include "thread.h"
#include "data.h"
#include "string.h"

struct Node {
    String name;
    array<Node*> children;
    Node(const string& name):name(copy(name)){}
};
bool operator ==(const Node& a, const Node& b) { return a.name==b.name; }
bool operator ==(const Node& a, const string& b) { return a.name==b; }
String str(const Node& o) { String s = copy(o.name); if(o.children) s<<'{'<<str(o.children)<<'}'; return s; }

String dot(array<const Node*>& once, const Node& o) {
    String s;
    if(!once.contains(&o)) {
        once << &o;
        s<<'"'<<o.name<<'"'<<"[shape=record];\n"_;
        for(Node* dep: o.children) { s<<'"'<<o.name<<"\" -> \""_<<dep->name<<"\"\n"_<<dot(once, *dep); }
    }
    return s;
}

void generateSVG(const Node& root, const string& folder){
    string name = root.name.contains('/') ? section(root.name,'/',-2,-1) : string(root.name);
    String s ("digraph \""_+name+"\" {\n"_);
    array<const Node*> once;
    s << ::dot(once, root);
    s << "}"_;
    String path = "/var/tmp/"_+name+".dot"_;
    writeFile(path, s);
    ::execute("/usr/bin/dot"_,{path,"-Tsvg"_,"-o"_+folder+"/"_+name+".svg"_});
}

struct Build {
    string target = arguments().size>=1?arguments()[0]:"test"_;
    string build = arguments().size>=2?arguments()[1]:"debug"_;
    string install = arguments().size>=3?arguments()[2]:""_;
    bool graph = build=="graph"_, compile = !graph;
    const Folder& folder = currentWorkingDirectory();
    const string tmp = "/var/tmp/"_;
    array<unique<Node>> modules;
    array<String> libraries;
    array<String> files;
    array<int> pids;

    array<String> sources = folder.list(Files|Recursive);
    /// Returns the first path matching file
    String find(const string& file) { for(String& path: sources) if(section(path,'/',-2,-1)==file) return String(path.contains('.')?section(path,'.',0,-2):path); return String(); }

    /// Returns timestamp of the last modified interface header recursively parsing includes
    int64 parseHeader(const string& name) {
        File file (name+".h"_, folder);
        int64 lastEdit = file.modifiedTime();
        for(TextData s = file.read(file.size()); s; s.advance(1)) {
            if(s.match("#include \""_)) {
                string name = s.until('.');
                String header = find(name+".h"_);
                if(header) lastEdit = max(lastEdit, parseHeader(header));
            }
        }
        return lastEdit;
    }

    /// Compiles a module and its dependencies as needed
    /// \return Timestamp of the last modified module implementation (deep)
    int64 processModule(const string& target) {
        assert_(target);
        modules << unique<Node>(target);
        Node& parent = modules.last();
        File file (target+".cc"_, folder);
        int64 lastCompileEdit = file.modifiedTime(); // including headers
        int64 lastLinkEdit = lastCompileEdit; // including headers and their associated implementations (avoid getting all timestamps again before link)
        for(TextData s = file.read(file.size()); s; s.advance(1)) {
            if(s.match("#include "_)) {
                if(s.match('"')) { // module header
                    string name = s.until('.');
                    assert_(name);
                    String header = find(name+".h"_);
                    if(header) lastCompileEdit = max(lastCompileEdit, parseHeader(header));
                    String module = find(name+".cc"_);
                    if(!module || module == parent) continue;
                    if(!modules.contains(module)) lastLinkEdit = max(lastLinkEdit, max(lastCompileEdit, processModule(module)));
                    parent.children << modules[modules.indexOf(module)].pointer;
                } else { // library header
                    for(;s.peek()!='\n';s.advance(1)) if(s.match("//"_)) {
                        string library=s.identifier("_"_);
                        if(library) { assert(s.peek()=='\n',s.until('\n')); libraries += String(library); }
                        break;
                    }
                }
            }
            if(s.match("FILE("_) || s.match("ICON("_)) {
                string file = s.identifier("_-"_);
                assert_(file && !files.contains(file), file);
                files << String(file);
                s.skip(")"_);
            }
        }
        if(compile) {
            String object = tmp+build+"/"_+target+".o"_;
            if(!existsFile(object, folder) || lastCompileEdit >= File(object).modifiedTime()) {
                static const array<string> flags = split("-c -pipe -std=c++11 -Wall -Wextra -I/usr/include/freetype2 -march=native -o"_);
                array<String> args;
                args << copy(object) << target+".cc"_ << "-DBUILD=\""_+build+"\""_;
                if(::find(build,"debug"_)) args << String("-g"_) << String("-Og"_) << String("-fno-omit-frame-pointer") << String("-DASSERT"_);
                else if(::find(build,"assert"_)) args << String("-g"_) << String("-O3"_) << String("-DASSERT"_);
                else if(::find(build,"fast"_)) args << String("-g"_) << String("-O3"_);
                else if(::find(build,"release"_)) args << String("-O3"_);
                else error("Unknown build",build);
                args << apply(folder.list(Folders), [this](const String& subfolder){ return "-iquote"_+subfolder; });
                log(target);
                while(pids.size>=coreCount-1) { // Waits for a job to finish before launching a new unit
                    int pid =  wait(); // Waits for any child to terminate
                    if(wait(pid)) fail();
                    pids.remove(pid);
                }
                pids << execute("/usr/bin/g++"_,flags+toRefs(args), false); //TODO: limit to 8
            }
        }
        return lastLinkEdit;
    }

    void fail() { log("Build failed"_); exit(-1); exit_thread(-1); }

    Build() {
        Folder(tmp+build, root(), true);
        for(string subfolder: folder.list(Folders|Recursive)) Folder(tmp+build+"/"_+subfolder, root(), true);
        int64 lastEdit = processModule( find(target+".cc"_) );
        if(compile) {
            if(files) {
                Folder(tmp+"files"_, root(), true);
                for(String& file: files) {
                    String path = find(replace(file,"_"_,"/"_));
                    assert_(path, "No such file to embed", file);
                    Folder subfolder = Folder(section(path,'/',0,-2), folder);
                    string name = section(path,'/',-2,-1);
                    String object = tmp+"files/"_+name+".o"_;
                    int64 lastFileEdit = File(name, subfolder).modifiedTime();
                    if(!existsFile(object) || lastFileEdit >= File(object).modifiedTime()) {
                        log(name);
                        if(execute("/usr/bin/ld"_,split("-r -b binary -o"_)<<object<<name, true, subfolder)) fail();
                    }
                    lastEdit = max(lastEdit, lastFileEdit);
                    file = move(object);
                }
            }
            string name = target;
            String binary = tmp+build+"/"_+name+"."_+build;
            if(!existsFile(binary) || lastEdit >= File(binary).modifiedTime()) {
                array<String> args; args<<String("-o"_)<<copy(binary);
                args << apply(modules, [this](const unique<Node>& module){ return tmp+build+"/"_+module->name+".o"_; });
                args << copy(files);
                args << apply(libraries, [this](const String& library){ return "-l"_+library; });
                for(int pid: pids) if(wait(pid)) fail(); // Wait for each translation unit to finish compiling before final linking
                log(name);
                if(execute("/usr/bin/g++"_,toRefs(args))) fail();
            }
            if(install && (!existsFile(name, install) || File(binary).modifiedTime() > File(name, install).modifiedTime())) copy(root(), binary, install, name);
        }
        if(graph) generateSVG(modules.first(), getenv("HOME"_));
    }
} build;
