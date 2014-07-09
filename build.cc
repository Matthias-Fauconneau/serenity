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

// Locates an executable
String which(string name) {
    if(!name) return {};
    if(existsFile(name)) return String(name);
    for(string folder: split(getenv("PATH"_,"/usr/bin"_),':')) if(existsFolder(folder) && existsFile(name, folder)) return folder+"/"_+name;
    return {};
}

struct Build {
    const Folder folder {"."_};
    String tmp;
    String target;
    array<String> defines;
    array<string> flags;
    string arch;
    String CXX;
    String LD = which("ld"_);
    bool needLink = false;
    array<unique<Node>> modules;
    array<String> libraries;
    array<String> files;
    array<int> pids;

    array<String> sources = folder.list(Files|Recursive);
    /// Returns the first path matching file
    String find(const string& file) { for(String& path: sources) if(section(path,'/',-2,-1)==file) return String(path.contains('.')?section(path,'.',0,-2):path); return String(); }

    string tryParseIncludes(TextData& s) {
        if(!s.match("#include "_) && !s.match("//#include "_)) return ""_;
        if(s.match('"')) { // module header
            string name = s.until('.');
            return name;
        } else { // library header
            for(;s.peek()!='\n';s.advance(1)) if(s.match("//"_)) {
                string library=s.identifier("_"_);
                if(library) { assert(s.peek()=='\n',s.until('\n')); libraries += String(library); }
                break;
            }
            return ""_;
        }
    }
    void tryParseDefines(TextData& s) {
        if(!s.match("#define "_)) return;
        string id = s.identifier("_"_);
        s.whileAny(" "_);
        if(!s.match('0')) defines << toLower(id);
    }
    void tryParseConditions(TextData& s) {
        if(!s.match("#if "_)) return;
        bool condition = !s.match('!');
        string id = s.identifier("_"_);
        bool value = false;
        if(id=="1"_) value=true;
        else if(id=="__arm__"_ && arch=="arm"_) value=true;
        else if(id=="__i386"_ && arch=="atom"_) value=true;
        else if(id=="__x86_64"_ && arch=="x64"_) value=true;
        else if(flags.contains(toLower(id))) value=true; // Conditionnal build (extern use flag)
        else if(defines.contains(toLower(id))) value=true; // Conditionnal build (intern use flag)
        else if(arch=="native"_) {
#if __i386
            if(id=="__i386"_) value = true;
#endif
#if __x86_64
            if(id=="__x86_64"_) value = true;
#endif
#if __arm__
            if(id=="__arm__"_) value = true;
#endif
        }
        if(value != condition) {
            for(; !s.match("#endif"_); s.line()) tryParseConditions(s);
        }
    }
    bool tryParseFiles(TextData& s) {
        if(!s.match("FILE("_) && !s.match("ICON("_)) return false;
        string name = s.identifier("_-"_);
        s.skip(")"_);

        String filesPath = tmp+"/files."_+arch;
        Folder(filesPath, root(), true);
        String path = find(replace(name,"_"_,"/"_));
        assert(path, "No such file to embed", name);
        Folder subfolder = Folder(section(path,'/',0,-2), folder);
        string file = section(path,'/',-2,-1);
        String object = filesPath+"/"_+file+".o"_;
        assert_(!files.contains(object), name);
        int64 lastFileEdit = File(file, subfolder).modifiedTime();
        if(!existsFile(object) || lastFileEdit >= File(object).modifiedTime()) {
            if(execute(LD, split((arch=="atom"_?"--oformat elf32-i386 "_:""_)+"-r -b binary -o"_)<<object<<file, true, subfolder)) fail();
            needLink = true;
        }
        files << move(object);
        return true;
    }

    /// Returns timestamp of the last modified interface header recursively parsing includes
    int64 parse(const string& name, Node& parent) {
        File file(name, folder);
        int64 lastEdit = file.modifiedTime();
        for(TextData s = file.read(file.size()); s; s.line()) {
            string name = tryParseIncludes(s);
            if(name) {
                String header = find(name+".h"_);
                if(header) lastEdit = max(lastEdit, parse(header+".h"_, parent));
                String module = find(name+".cc"_);
                if(!module || module == parent) continue;
                if(!modules.contains(module)) compileModule(module);
                parent.children << modules[modules.indexOf(module)].pointer;
            }
            tryParseDefines(s);
            tryParseConditions(s);
            do { s.whileAny(" "_); } while(tryParseFiles(s));
        }
        return lastEdit;
    }

    /// Compiles a module and its dependencies as needed
    /// \return Timestamp of the last modified module implementation (deep)
    void compileModule(const string& target) {
        assert(target);
        modules << unique<Node>(target);
        Node& module = modules.last();
        int64 lastEdit = parse(target+".cc"_, module);
        String object = tmp+"/"_+join(flags,"-"_)+"/"_+target+".o"_;
        if(!existsFile(object, folder) || lastEdit >= File(object).modifiedTime()) {
            array<String> args;
            args << copy(object) << target+".cc"_;
            if(arch=="arm"_) args << String("-I/buildroot/output/host/usr/arm-buildroot-linux-uclibcgnueabihf/sysroot/usr/include/freetype2"_);
            else args << String("-I/usr/include/freetype2"_);
            if(arch=="arm"_) {}
            else if(arch=="atom"_) args << String("-m32"_) << String("-march=atom"_) << String("-mfpmath=sse"_);
            else args << String("-march=native"_);

            if(!flags.contains("release"_)) args << String("-g"_);
            if(!flags.contains("debug"_)) args << String("-O3"_);
            if(flags.contains("profile"_)) {
                args << String("-finstrument-functions"_);
                if(!endsWith(CXX,"clang++"_)) args << String("-finstrument-functions-exclude-file-list=core,array,string,time,map,trace,profile"_);
            }
            for(string flag: flags) args << "-D"_+toUpper(flag)+"=1"_;
            args << apply(folder.list(Folders), [this](const String& subfolder){ return "-iquote"_+subfolder; });
            log(target);
            while(pids.size>=1) { // Waits for a job to finish before launching a new unit
                int pid = wait(); // Waits for any child to terminate
                if(wait(pid)) fail();
                pids.remove(pid);
            }
            Folder(tmp+"/"_+join(flags,"-"_)+"/"_+section(target,'/',0,-2), root(), true);
            {static const array<string> flags = split("-c -pipe -std=c++11 -Wall -Wextra -Wno-overloaded-virtual -o"_);
                pids << execute(CXX, flags+toRefs(args), false);}
            needLink = true;
        }
        files << tmp+"/"_+join(flags,"-"_)+"/"_+target+".o"_;
    }

    void fail() { log("Build failed"_); exit(-1); exit_thread(-1); }

    Build() {
        // Configures
        CXX = which(getenv("CC"_));
        if(!CXX) CXX=which("clang++"_);
        if(!CXX) CXX=which("g++4.9"_);
        if(!CXX) CXX=which("g++4.8"_);
        if(!CXX) CXX=which("g++"_);

        string install;
        for(string arg: arguments()) {
            if(startsWith(arg,"/"_)) install=arg;
            else if(find(arg+".cc"_)) {
                if(target) log("Multiple targets unsupported, building last target:",arg);
                target = String(arg);
            } else flags << split(arg,'-');
        }

        arch = flags.contains("arm"_) ? "arm"_ : flags.contains("atom"_) ? "atom"_ : "native"_;
        if(arch=="arm"_) CXX = which("arm-buildroot-linux-uclibcgnueabihf-g++"_), LD = which("arm-buildroot-linux-uclibcgnueabihf-ld"_);
        //else if(flags.contains("profile"_)) CXX=which("g++"_); //FIXME: Clang does not support instrument-functions-exclude-file-list
        const String base (section(folder.name(),'/',-2,-1));
        if(!target) target = copy(base);

        tmp = "/var/tmp/"_+base+"."_+section(CXX,'/',-2,-1);
        Folder(tmp, root(), true);
        Folder(tmp+"/"_+join(flags,"-"_), root(), true);

        // Compiles
        if(flags.contains("profile"_)) compileModule(find("profile.cc"_));
        compileModule( find(target+".cc"_) );

        // Links
        String binary = tmp+"/"_+join(flags,"-"_)+"/"_+target;
        if(existsFolder(binary)) binary << '.' << arch;
        if(!existsFile(binary) || needLink) {
            array<String> args; args<<String("-o"_)<<copy(binary);
            if(flags.contains("atom"_)) args<<String("-m32"_);
            args << copy(files);
            args << apply(libraries, [this](const String& library){ return "-l"_+library; });
            for(int pid: pids) if(wait(pid)) fail(); // Wait for each translation unit to finish compiling before final linking
            if(execute(CXX, toRefs(args))) fail();
        }

        // Installs
        if(install && (!existsFile(target, install) || File(binary).modifiedTime() > File(target, install).modifiedTime())) copy(root(), binary, install, target);
    }
} build;
