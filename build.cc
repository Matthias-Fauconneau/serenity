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

struct Build {
    string target = arguments().size>=1?arguments()[0]:"test"_;
    array<String> defines;
    array<string> flags;
    const Folder& folder = currentWorkingDirectory();
    const string tmp = "/var/tmp/"_;
    string CXX = existsFile("/usr/bin/clang++"_) ? "/usr/bin/clang++"_ : existsFile("/usr/bin/g++-4.8"_) ? "/usr/bin/g++-4.8"_ : "/usr/bin/g++"_;
    string LD = "/usr/bin/ld"_;
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
        assert(target);
        modules << unique<Node>(target);
        Node& parent = modules.last();
        File file (target+".cc"_, folder);
        int64 lastCompileEdit = file.modifiedTime(); // including headers
        int64 lastLinkEdit = lastCompileEdit; // including headers and their associated implementations (avoid getting all timestamps again before link)
        for(TextData s = file.read(file.size()); s; s.advance(1)) {
            if(s.match("#include "_)) {
                if(s.match('"')) { // module header
                    string name = s.until('.');
                    assert(name);
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
            if(s.match("#define "_)) {
                string id = s.identifier("_"_);
                s.whileAny(" "_);
                if(!s.match('0')) defines << toLower(id);
            }
            if(s.match("#if "_)) {
                bool condition = !s.match('!');
                string id = s.identifier("_"_);
                bool value = false;
                if(id=="__x86_64"_ && !flags.contains("atom"_)) value=true;
                else if(flags.contains(toLower(id))) value=true; // Conditionnal build (extern use flag)
                else if(defines.contains(toLower(id))) value=true; // Conditionnal build (intern use flag)
                if(value != condition) s.until("#endif"_); // FIXME: Nesting unsupported
            }
            if(s.match("FILE("_) || s.match("ICON("_)) {
                string file = s.identifier("_-"_);
                assert(file && !files.contains(file), file);
                files << String(file);
                s.skip(")"_);
            }
        }
        String object = tmp+join(flags," "_)+"/"_+target+".o"_;
        if(!existsFile(object, folder) || lastCompileEdit >= File(object).modifiedTime()) {
            array<String> args;
            args << copy(object) << target+".cc"_;
            if(flags.contains("atom"_)) args << String("-m32"_) << String("-march=atom"_) << String("-mfpmath=sse"_);
            else if(flags.contains("arm"_)) args << String("-I/buildroot/output/host/usr/arm-buildroot-linux-gnueabihf/sysroot/usr/include/freetype2"_);
            else args << String("-march=native"_) << String("-I/usr/include/freetype2"_);
            if(!flags.contains("release"_)) args << String("-g"_);
            if(flags.contains("debug"_)) args << String("-fno-omit-frame-pointer"_);
            else args << String("-O3"_);
            for(string flag: flags) args << "-D"_+toUpper(flag)+"=1"_;
            args << apply(folder.list(Folders), [this](const String& subfolder){ return "-iquote"_+subfolder; });
            log(target);
            while(pids.size>=1) { // Waits for a job to finish before launching a new unit
                int pid = wait(); // Waits for any child to terminate
                if(wait(pid)) fail();
                pids.remove(pid);
            }
            {static const array<string> flags = split("-c -pipe -std=c++11 -Wall -Wextra -o"_);
                pids << execute(CXX, flags+toRefs(args), false);}
        }
        return lastLinkEdit;
    }

    void fail() { log("Build failed"_); exit(-1); exit_thread(-1); }

    Build() {
        string install;
        for(string arg: arguments().slice(1)) if(startsWith(arg,"/"_)) install=arg; else flags << arg;
        if(flags.contains("arm"_)) {
            CXX = "/buildroot/output/host/usr/bin/arm-buildroot-linux-gnueabihf-g++"_;
            LD = "/buildroot/output/host/usr/bin/arm-buildroot-linux-gnueabihf-ld"_;
        }

        Folder(tmp+join(flags," "_), root(), true);
        for(string subfolder: folder.list(Folders|Recursive)) Folder(tmp+join(flags," "_)+"/"_+subfolder, root(), true);
        int64 lastEdit = processModule( find(target+".cc"_) );
        if(files) {
            String filesPath = tmp+"files"_+(flags.contains("arm"_)?".arm"_:flags.contains("atom"_)?".x32"_:".x64"_);
            Folder(filesPath, root(), true);
            for(String& file: files) {
                String path = find(replace(file,"_"_,"/"_));
                assert(path, "No such file to embed", file);
                Folder subfolder = Folder(section(path,'/',0,-2), folder);
                string name = section(path,'/',-2,-1);
                String object = filesPath+"/"_+name+".o"_;
                int64 lastFileEdit = File(name, subfolder).modifiedTime();
                if(!existsFile(object) || lastFileEdit >= File(object).modifiedTime()) {
                    log(name);
                    if(execute(LD, split((flags.contains("atom"_)?"--oformat elf32-i386 "_:""_)+"-r -b binary -o"_)<<object<<name, true, subfolder))
                        fail();
                }
                lastEdit = max(lastEdit, lastFileEdit);
                file = move(object);
            }
        }
        string name = target;
        String binary = tmp+join(flags," "_)+"/"_+name+"."_+join(flags," "_);
        if(!existsFile(binary) || lastEdit >= File(binary).modifiedTime()) {
            array<String> args; args<<String("-o"_)<<copy(binary);
            if(flags.contains("atom"_)) args<<String("-m32"_);
            args << apply(modules, [this](const unique<Node>& module){ return tmp+join(flags," "_)+"/"_+module->name+".o"_; });
            args << copy(files);
            args << apply(libraries, [this](const String& library){ return "-l"_+library; });
            for(int pid: pids) if(wait(pid)) fail(); // Wait for each translation unit to finish compiling before final linking
            if(execute(CXX, toRefs(args))) fail();
        }
        if(install && (!existsFile(name, install) || File(binary).modifiedTime() > File(name, install).modifiedTime())) rename(root(), binary, install, name);
    }
} build;
