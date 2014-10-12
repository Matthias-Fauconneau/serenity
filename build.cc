/// \file build.cc Builds C++ projects with automatic module dependency resolution
#include "thread.h"
#include "string.h"
#include "data.h"
#include "time.h" //DEBUG

struct Node {
    String name;
    array<Node*> children;
    explicit Node(const string name):name(copy(name)){}
};
//bool operator ==(const Node& a, const Node& b) { return a.name==b.name; }
bool operator ==(const Node& a, const string b) { return a.name==b; }

struct Build {
    // Parameters
    String CXX = which(getenv("CC")) ?: which("clang++") ?: which("g++");
    String LD = which("ld");

    const Folder folder {"."};
    const String base {section(folder.name(),'/',-2,-1)};
    const String tmp = "/var/tmp/"+base+"."+section(CXX,'/',-2,-1);

    String target;
    array<string> flags;
    array<String> args = apply(folder.list(Folders), [this](const string subfolder){ return String("-iquote"+subfolder); });

    // Variables
    array<String> defines;
    array<unique<Node>> modules;
    array<String> files;
    array<String> libraries;
    array<int> pids;
    bool needLink = false;

    array<String> sources = folder.list(Files|Recursive);
    /// Returns the first path matching file
    String find(const string file) {
        for(string path: sources) {
            if(section(path,'/',-2,-1) == file) return String( path.contains('.') ? section(path,'.',0,-2) : path );
        }
        return {};
    }

    string tryParseIncludes(TextData& s) {
        if(!s.match("#include ") && !s.match("//#include ")) return "";
        if(s.match('"')) { // module header
            string name = s.until('.');
            return name;
        } else { // library header
            s.skip('<');
            s.whileNo(">\n");
            s.skip('>');
            s.whileAny(' ');
            if(s.match("//")) {
                for(;;) {
                    s.whileAny(' ');
                    string library=s.identifier("_");
                    if(!library) break;
                    if(!libraries.contains(library)) libraries.append(String(library));
                }
                assert_(s.wouldMatch('\n'));
            }
        }
        return "";
    }
    void tryParseDefines(TextData& s) {
        if(!s.match("#define ")) return;
        string id = s.identifier("_");
        s.whileAny(" ");
        if(!s.match('0')) defines.append( toLower(id) );
    }
    bool tryParseConditions(TextData& s, string fileName) {
        if(!s.match("#if ")) return false;
        bool condition = !s.match('!');
        string id = s.identifier("_");
        bool value = false;
        if(id=="1") value=true;
        else if(flags.contains(toLower(id))) value=true; // Conditionnal build (extern use flag)
        else if(defines.contains(toLower(id))) value=true; // Conditionnal build (intern use flag)
        if(value != condition) {
            while(!s.match("#endif")) {
                assert_(s, fileName+": Expected #endif, got EOD");
                if(!tryParseConditions(s, fileName)) s.line();
            }
        }
        return true;
    }
    bool tryParseFiles(TextData& s) {
        if(!s.match("FILE(") && !s.match("ICON(")) return false;
        string name = s.identifier("_-");
        s.skip(')');

        String filesPath = tmp+"/files";
        Folder(filesPath, root(), true);
        String path = find(String(name).replace('_','/'));
        assert(path, "No such file to embed", name);
        Folder subfolder = Folder(section(path,'/',0,-2), folder);
        string file = section(path,'/',-2,-1);
        String object = filesPath+"/"+file+".o";
        assert_(!files.contains(object), name);
        int64 lastFileEdit = File(file, subfolder).modifiedTime();
        if(!existsFile(object) || lastFileEdit >= File(object).modifiedTime()) {
            array<string> args ({"-r", "-b", "binary", "-o", object, file});
            if(execute(LD, args, true, subfolder)) error("Failed to embed");
            needLink = true;
        }
        files.append( move(object) );
        return true;
    }

    /// Returns timestamp of the last modified interface header recursively parsing includes
    int64 parse(const string fileName, Node& parent) {
        File file(fileName, folder);
        int64 lastEdit = file.modifiedTime();
        for(TextData s = file.read(file.size()); s; s.line()) {
            {string name = tryParseIncludes(s);
                if(name) {
                    String header = find(name+".h");
                    if(header) lastEdit = max(lastEdit, parse(header+".h", parent));
                    String module = find(name+".cc");
                    if(!module || parent == module) continue;
                    if(!modules.contains(module)) compileModule(module);
                    parent.children.append( modules[modules.indexOf(module)].pointer );
                }
            }
            tryParseDefines(s);
            tryParseConditions(s, fileName);
            do { s.whileAny(" "); } while(tryParseFiles(s));
        }
        return lastEdit;
    }

    /// Compiles a module and its dependencies as needed
    /// \return Timestamp of the last modified module implementation (deep)
    void compileModule(const string target) {
        assert_(target);
        modules.append( unique<Node>(target) );
        Node& module = modules.last();
        String fileName = target+".cc";
        int64 lastEdit = parse(fileName, module);
        String object = tmp+"/"+join(flags,"-")+"/"+target+".o";
        if(!existsFile(object, folder) || lastEdit >= File(object).modifiedTime()) {
            while(pids.size>=4) { // Waits for a job to finish before launching a new unit
                int pid = wait(); // Waits for any child to terminate
                if(wait(pid))  error("Failed to compile");
                pids.remove(pid);
            }
            Folder(tmp+"/"+join(flags,"-")+"/"+section(target,'/',0,-2), root(), true);
            log(target);
            pids.append( execute(CXX, ref<string>{"-c", "-pipe", "-std=c++1y", "-Wall", "-Wextra", "-Wno-overloaded-virtual",
                                                  "-march=native", "-o" , object, fileName, "-I/usr/include/freetype2"}+toRefs(args), false) );
            needLink = true;
        }
        files.append( tmp+"/"+join(flags,"-")+"/"+target+".o" );
    }

    Build() {
        // Configures
        string install;
        for(string arg: arguments()) {
            if(startsWith(arg,"/")) install=arg;
            else if(find(arg+".cc")) {
                if(target) log("Multiple targets unsupported, building last target:",arg);
                target = String(arg);
            } else flags.append( split(arg,"-") );
        }

        if(!target) target = copy(base);

        for(string flag: flags) args.append( "-D"+toUpper(flag)+"=1" );
        if(!flags.contains("release")) args.append( String("-g") );
        if(!flags.contains("debug")) args.append( String("-O3") );
        else if(flags.contains("fast")) args.append( String("-O1") ); // fast-debug
        if(flags.contains("profile")) args.append( String("-finstrument-functions") );

        Folder(tmp, root(), true);
        Folder(tmp+"/"+join(flags,"-"), root(), true);

        // Compiles
        if(flags.contains("profile")) compileModule(find("profile.cc"));
        compileModule( find(target+".cc") );

        // Links
        String binary = tmp+"/"+join(flags,"-")+"/"+target;
        if(existsFolder(binary)) binary.append(".elf");
        if(!existsFile(binary) || needLink) {
            array<String> args = move(files);
            args.append(String("-o"));
            args.append(copy(binary));
            args.append(apply(libraries, [this](const String& library){ return String("-l"+library); }));
            for(int pid: pids) if(wait(pid)) error("Failed to compile"); // Waits for each translation unit to finish compiling before final linking
            if(execute(CXX, toRefs(args))) error("Failed to link");
        }

        // Installs
        if(install && (!existsFile(target, install) || File(binary).modifiedTime() > File(target, install).modifiedTime())) copy(root(), binary, install, target);
    }
} build;
