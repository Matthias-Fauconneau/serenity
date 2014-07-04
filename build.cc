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
    if(existsFile(name)) return copy(String(name));
    for(string folder: split(getenv("PATH"_,"/usr/bin"_),':')) if(existsFile(name, folder)) return folder+"/"_+name;
    return {};
}

struct Build {
    const Folder folder {"."_};
    const String base = copy(String(section(folder.name(),'/',-2,-1)));
    const string target = arguments().size>=1 ? arguments()[0] : (string)base;
    array<String> defines;
    array<string> flags;
    const String tmp {"/var/tmp/"_+base};
    String CXX;
    String LD = which("ld"_);
    bool needLink = false;
    array<unique<Node>> modules;
    array<String> libraries;
    array<String> files;
    array<int> pids;

    array<String> sources = folder.list(Files|Recursive);
    /// Returns the first path matching file
    string find(const string& file) { for(String& path: sources) if(section(path,'/',-2,-1)==file) return path.contains('.')?section(path,'.',0,-2):path; return ""_; }

    string tryParseIncludes(TextData& s) {
        if(!s.match("#include "_) && !s.match("//#include "_)) return ""_;
        if(s.match('"')) { // module header
            string name = s.until('.');
            return name;
        } else { // library header
            for(;s.peek()!='\n';s.advance(1)) if(s.match("//"_)) {
                s.whileAny(" \t"_);
                string library=s.identifier("_"_);
                if(library) { assert(s.peek()=='\n',s.until('\n')); libraries += copy(String(library)); }
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
        else if(id=="__arm__"_ && flags.contains("arm"_)) value=true;
        else if(id=="__x86_64"_ && (!flags.contains("atom"_) && !flags.contains("arm"_))) value=true;
        else if(flags.contains(toLower(id))) value=true; // Conditionnal build (extern use flag)
        else if(defines.contains(toLower(id))) value=true; // Conditionnal build (intern use flag)
        if(value != condition) {
            for(;;) {
                if(s.match("#endif"_)) break;
                if(s.match("#else"_)) break;
                assert_(s, "Unterminated #if");
                s.line();
            }
            tryParseConditions(s);
        }
    }
    bool tryParseFiles(TextData& s) {
        string suffix;
        if(s.match("ICON("_)) {}
        else if(s.match("CL("_)) suffix=".cl"_;
        else return false;
        String file = s.identifier("_-"_)+suffix;
        s.until(")"_);

        String filesPath = tmp+"/files"_+(flags.contains("arm"_)?".arm"_:flags.contains("atom"_)?".x32"_:".x64"_);
        Folder(filesPath, root(), true);
        string path = find(file);
        assert(path, "No such file to embed", file);
        Folder subfolder = Folder(section(path,'/',0,-2), folder);
        String object = filesPath+"/"_+file+".o"_;
        if(!files.contains(object)) {
            int64 lastFileEdit = File(file, subfolder).modifiedTime();
            if(!existsFile(object) || lastFileEdit >= File(object).modifiedTime()) {
                if(execute(LD, split((flags.contains("atom"_)?"--oformat elf32-i386 "_:""_)+"-r -b binary -o"_)<<object<<file, true, subfolder)) fail();
                needLink = true;
            }
            files << move(object);
        }
        return true;
    }

    /// Returns timestamp of the last modified interface header recursively parsing includes
    int64 parse(const string& name, Node& parent) {
        File file(name, folder);
        int64 lastEdit = file.modifiedTime();
        for(TextData s = file.read(file.size()); s;) {
            string name = tryParseIncludes(s);
            if(name) {
                string header = find(name+".h"_);
                if(header) lastEdit = max(lastEdit, parse(header+".h"_, parent));
                string module = find(name+".cc"_);
                if(!module || module == parent) continue;
                if(!modules.contains(module)) compileModule(module);
                parent.children << modules[modules.indexOf(module)].pointer;
            }
            tryParseDefines(s);
            tryParseConditions(s);
            while(s && !s.match('\n')) if(!tryParseFiles(s)) s.next();
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
            args << String((string)object) << target+".cc"_;
            if(flags.contains("arm"_)) args << String("-I/buildroot/output/host/usr/arm-buildroot-linux-uclibcgnueabihf/sysroot/usr/include/freetype2"_);
            else args << String("-I/usr/include/freetype2"_);
            if(flags.contains("arm"_)) {}
            else if(flags.contains("atom"_)) args << String("-m32"_) << String("-march=atom"_) << String("-mfpmath=sse"_);
            else args << String("-march=corei7-avx"_); //String("-march=native"_);

            if(!flags.contains("release"_)) args << String("-g"_);
            if(!flags.contains("debug"_)) args << String("-O3"_); // O3 / Ofast
            if(flags.contains("profile"_)) {
                args << String("-finstrument-functions"_);
                if(!endsWith(CXX,"clang++"_))
                    args << String("-finstrument-functions-exclude-file-list=core,array,string,trace,profile"_);
            }
            for(string flag: flags) args << "-D"_+toUpper(flag)+"=1"_;
            args << apply(folder.list(Folders), [this](const string& subfolder)->String{ return "-iquote"_+subfolder; });
            log(target);
            while(pids.size>=1) { // Waits for a job to finish before launching a new unit
                int pid = wait(); // Waits for any child to terminate
                if(wait(pid)) fail();
                pids.remove(pid);
            }
            if(startsWith(section(CXX,'/',-2,-1),"g++"_)) args << String("-fabi-version=0"_);
            {static const array<string> flags = split("-c -pipe -std=c++1y -Wall -Wextra -Wno-overloaded-virtual -Wno-deprecated-declarations -o"_);
                pids << execute(CXX, flags+toRefs(args), false);}
            needLink = true;
        }
        files << tmp+"/"_+join(flags,"-"_)+"/"_+target+".o"_;
    }

    void fail() { log("Build failed"_); exit(-1); exit_thread(-1); }

    Build() {
        CXX = which(getenv("CC"_));
        if(!CXX) CXX=which("clang++"_);
        if(!CXX) CXX=which("g++4.8"_);
        if(!CXX) CXX=which("g++"_);

        string install;
        if(arguments().size>1) { for(string arg: arguments().slice(1)) if(startsWith(arg,"/"_)) install=arg; else flags << split(arg,'-'); }
        if(flags.contains("profile"_)) CXX=which("g++"_); //FIXME: Clang does not support instrument-functions-exclude-file-list
        if(flags.contains("arm"_)) {
            CXX = which("arm-buildroot-linux-uclibcgnueabihf-g++"_);
            LD = which("arm-buildroot-linux-uclibcgnueabihf-ld"_);
        }

        Folder(tmp+"/"_+join(flags,"-"_), root(), true);
        for(string subfolder: folder.list(Folders|Recursive)) Folder(tmp+"/"_+join(flags,"-"_)+"/"_+subfolder, root(), true);
        if(flags.contains("profile"_)) compileModule(find("profile.cc"_));
        compileModule( find(target+".cc"_) );
        String binary = tmp+"/"_+join(flags,"-"_)+"/"_+target+(flags?"."_:""_)+join(flags,"-"_);
        if(!existsFile(binary) || needLink) {
            array<String> args; args<<String("-o"_)<<String((string)binary);
            if(flags.contains("atom"_)) args<<String("-m32"_);
            //args << apply(modules, [this](const unique<Node>& module){ return tmp+"/"_+join(flags,"-"_)+"/"_+module->name+".o"_; });
            args << apply(files, [](const string& file){return String(file);});
            args << apply(libraries, [this](const String& library)->String{ return "-l"_+library; });
            for(int pid: pids) if(wait(pid)) fail(); // Wait for each translation unit to finish compiling before final linking
            if(execute(CXX, toRefs(args))) fail();
        }
        if(install && (!existsFile(target, install) || File(binary).modifiedTime() > File(target, install).modifiedTime())) copy(root(), binary, install, target);
    }
} build;
