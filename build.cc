/// \file build.cc Builds C++ projects with automatic module dependency resolution
#include "thread.h"
#include "string.h"
#include "data.h"

struct Node {
    String name;
    array<Node*> children;
    Node(const string name):name(copy(name)){}
};
bool operator ==(const Node& a, const Node& b) { return a.name==b.name; }
bool operator ==(const Node& a, const string b) { return a.name==b; }

struct Build {
    // Parameters
    String CXX = which(getenv("CC"_)) ?: which("clang++"_) ?: which("g++"_);
    String LD = which("ld"_);

    const Folder folder {"."_};
    const String base {section(folder.name(),'/',-2,-1)};
    const String tmp = "/var/tmp/"_+base+"."_+section(CXX,'/',-2,-1);

    String target;
    array<string> flags;
    array<String> args = apply(folder.list(Folders), [this](const string subfolder){ return String("-iquote"_+subfolder); });

    // Variables
    array<String> defines;
    array<unique<Node>> modules;
    array<String> files;
    array<String> libraries;
    array<int> pids;
    bool needLink = false;

    array<String> sources = folder.list(Files|Recursive);
    /// Returns the first path matching file
    String find(const string file) { for(String& path: sources) if(section(path,'/',-2,-1)==file) return String(path.contains('.')?section(path,'.',0,-2):path); return String(); }

    string tryParseIncludes(TextData& s) {
        if(!s.match("#include "_) && !s.match("//#include "_)) return ""_;
        if(s.match('"')) { // module header
            string name = s.until('.');
            return name;
        } else { // library header
            s.skip('<');
            s.whileNo(">\n"_);
            s.skip('>');
            s.whileAny(' ');
            if(s.match("//"_)) {
                for(;;) {
                    s.whileAny(' ');
                    string library=s.identifier("_"_);
                    if(!library) break;
                    if(!libraries.contains(library)) libraries.append(String(library));
                }
                assert_(s.wouldMatch('\n'));
            }
        }
        return ""_;
    }
    void tryParseDefines(TextData& s) {
        if(!s.match("#define "_)) return;
        string id = s.identifier("_"_);
        s.whileAny(" "_);
        if(!s.match('0')) defines.append( toLower(id) );
    }
    bool tryParseConditions(TextData& s, string fileName) {
        if(!s.match("#if "_)) return false;
        bool condition = !s.match('!');
        string id = s.identifier("_"_);
        bool value = false;
        if(id=="1"_) value=true;
        else if(flags.contains(toLower(id))) value=true; // Conditionnal build (extern use flag)
        else if(defines.contains(toLower(id))) value=true; // Conditionnal build (intern use flag)
        if(value != condition) {
            while(!s.match("#endif"_)) {
                assert_(s, fileName+": Expected #endif, got EOD"_);
                if(!tryParseConditions(s, fileName)) s.line();
            }
        }
        return true;
    }
    bool tryParseFiles(TextData& s) {
        if(!s.match("FILE("_) && !s.match("ICON("_)) return false;
        string name = s.identifier("_-"_);
        s.skip(')');

        String filesPath = tmp+"/files"_;
        Folder(filesPath, root(), true);
        String path = find(String(name).replace('_','/'));
        assert(path, "No such file to embed", name);
        Folder subfolder = Folder(section(path,'/',0,-2), folder);
        string file = section(path,'/',-2,-1);
        String object = filesPath+"/"_+file+".o"_;
        assert_(!files.contains(object), name);
        int64 lastFileEdit = File(file, subfolder).modifiedTime();
        if(!existsFile(object) || lastFileEdit >= File(object).modifiedTime()) {
            array<string> args ({"-r"_, "-b"_, "binary"_, "-o"_, object, file});
            if(execute(LD, args, true, subfolder)) error("Failed to embed");
            needLink = true;
        }
        files.append( move(object) );
        return true;
    }

    /// Returns timestamp of the last modified interface header recursively parsing includes
    int64 parse(const string fileName, Node& parent) {
        assert_(fileName);
        File file(fileName, folder);
        int64 lastEdit = file.modifiedTime();
        for(TextData s = file.read(file.size()); s; s.line()) {
            {string name = tryParseIncludes(s);
                if(name) {
                    String header = find(name+".h"_);
                    if(header) lastEdit = max(lastEdit, parse(header+".h"_, parent));
                    String module = find(name+".cc"_);
                    if(!module || module == parent) continue;
                    if(!modules.contains(module)) compileModule(module);
                    parent.children.append( modules[modules.indexOf(module)].pointer );
                }
            }
            tryParseDefines(s);
            tryParseConditions(s, fileName);
            do { s.whileAny(" "_); } while(tryParseFiles(s));
        }
        return lastEdit;
    }

    /// Compiles a module and its dependencies as needed
    /// \return Timestamp of the last modified module implementation (deep)
    void compileModule(const string target) {
        assert(target);
        modules.append( unique<Node>(target) );
        Node& module = modules.last();
        int64 lastEdit = parse(target+".cc"_, module);
        String object = tmp+"/"_+join(flags,"-"_)+"/"_+target+".o"_;
        if(!existsFile(object, folder) || lastEdit >= File(object).modifiedTime()) {
            array<String> args ({copy(object), target+".cc"_});

            log(target);
            while(pids.size>=4) { // Waits for a job to finish before launching a new unit
                int pid = wait(); // Waits for any child to terminate
                if(wait(pid))  error("Failed to compile");
                pids.remove(pid);
            }
            Folder(tmp+"/"_+join(flags,"-"_)+"/"_+section(target,'/',0,-2), root(), true);
            static const ref<string> flags {"-c"_,"-pipe"_,"-std=c++1y"_,"-Wall"_,"-Wextra"_,"-Wno-overloaded-virtual"_,
                        "-I/usr/include/freetype2"_,"-march=native"_,"-o"_};
            pids.append( execute(CXX, flags+toRefs(args), false) );
            needLink = true;
        }
        files.append( tmp+"/"_+join(flags,"-"_)+"/"_+target+".o"_ );
    }

    Build() {
        // Configures

        string install;
        for(string arg: arguments()) {
            if(startsWith(arg,"/"_)) install=arg;
            else if(find(arg+".cc"_)) {
                if(target) log("Multiple targets unsupported, building last target:",arg);
                target = String(arg);
            } else flags.append( arg );
        }

        if(!target) target = copy(base);

        for(string flag: flags) args.append( "-D"_+toUpper(flag)+"=1"_ );
        if(!flags.contains("release"_)) args.append( String("-g"_) );
        if(!flags.contains("debug"_)) args.append( String("-O3"_) );
        if(flags.contains("profile"_)) args.append( String("-finstrument-functions"_) );

        Folder(tmp, root(), true);
        Folder(tmp+"/"_+join(flags,"-"_), root(), true);

        // Compiles
        if(flags.contains("profile"_)) compileModule(find("profile.cc"_));
        compileModule( find(target+".cc"_) );

        // Links
        String binary = tmp+"/"_+join(flags,"-"_)+"/"_+target;
        if(existsFolder(binary)) binary.append(".elf"_);
        if(!existsFile(binary) || needLink) {
            array<String> args = move(files);
            args.append(String("-o"_));
            args.append(copy(binary));
            args.append(apply(libraries, [this](const String& library){ return String("-l"_+library); }));
            for(int pid: pids) if(wait(pid)) error("Failed to compile"); // Waits for each translation unit to finish compiling before final linking
            if(execute(CXX, toRefs(args))) error("Failed to link");
        }

        // Installs
        if(install && (!existsFile(target, install) || File(binary).modifiedTime() > File(target, install).modifiedTime())) copy(root(), binary, install, target);
    }
} build;
