/// \file build.cc Builds C++ projects with automatic module dependency resolution
#include "thread.h"
#include "string.h"
#include "data.h"
#include "time.h" //DEBUG

struct Node {
	String name;
	array<Node*> edges;
	explicit Node(String&& name):name(move(name)){}
};
bool operator ==(const Node* a, const string b) { return a->name==b; }
bool operator ==(const Node& a, const string b) { return a.name==b; }

struct Branch {
	String name;
	array<Branch> children;
	explicit Branch(String&& name) : name(move(name)) {}
};
String str(const Branch& o, int depth=0) {
	return repeat(" ", depth)+o.name+"\n"+join(apply(o.children, [=](const Branch& child){ return str(child, depth+1); }));
};

/// Breaks cycles (converts a directed graph to a directed acyclic graph (DAG))
Branch collect(const Node* source, array<const Node*>& stack, int maxDepth) {
	stack.append(source);
	Branch branch (copy(source->name));
	if(stack.size <= maxDepth) {
		for(const Node* child: source->edges) {
			if(!stack.contains(child))
				branch.children.append( collect(child, stack, maxDepth) );
		}
	}
	stack.pop();
	return branch;
}
Branch collect(const Node& source, int maxDepth) { array<const Node*> stack; return collect(&source, stack, maxDepth); }

struct Build {
    // Parameters
    String CXX = which(getenv("CC")) ?: which("clang++") ?: which("g++");
    String LD = which("ld");

	const Folder folder {"."};
	const String base = copyRef(section(folder.name(),'/',-2,-1));
    const String tmp = "/var/tmp/"+base+"."+section(CXX,'/',-2,-1);

	string target;
    array<string> flags;
	array<String> args = apply(folder.list(Folders), [this](string subfolder)->String{ return "-iquote"+subfolder; });

    // Variables
    array<String> defines;
    array<unique<Node>> modules;
    array<String> files;
    array<String> libraries;
    array<int> pids;
    bool needLink = false;

	const array<String> sources = folder.list(Files|Recursive);
    /// Returns the first path matching file
	String find(string file) {
		for(string path: sources) if(path == file) return copyRef(path.contains('.') ? section(path,'.',0,-2) : path); // Exact match
		for(string path: sources) if(section(path,'/',-2,-1) == file) return copyRef(path.contains('.') ? section(path,'.',0,-2) : path); // Sub match
        return {};
    }

	String tryParseIncludes(TextData& s, string fileName) {
		if(!s.match("#include ") && !s.match("//#include ")) return {};
        if(s.match('"')) { // module header
            string name = s.until('.');
			return copyRef(name);
        } else { // library header
            s.skip('<');
            s.whileNo(">\n");
            if(!s.match('>')) error(fileName+':'+str(s.lineIndex)+':', "Expected '>', got '"_+s.peek()+'\'');
            s.whileAny(' ');
            if(s.match("//")) {
                for(;;) {
                    s.whileAny(' ');
                    string library=s.identifier("_");
                    if(!library) break;
					if(!libraries.contains(library)) libraries.append(copyRef(library));
                }
                assert_(s.wouldMatch('\n'));
            }
        }
		return {};
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
		String path = find(replace(name, '_', '/'));
        assert(path, "No such file to embed", name);
        Folder subfolder = Folder(section(path,'/',0,-2), folder);
        string file = section(path,'/',-2,-1);
        String object = filesPath+"/"+file+".o";
        assert_(!files.contains(object), name);
        int64 lastFileEdit = File(file, subfolder).modifiedTime();
        if(!existsFile(object) || lastFileEdit >= File(object).modifiedTime()) {
			if(execute(LD, {"-r", "-b", "binary", "-o", object, file}, true, subfolder)) error("Failed to embed");
            needLink = true;
        }
        files.append( move(object) );
        return true;
    }

    /// Returns timestamp of the last modified interface header recursively parsing includes
	int64 parse(string fileName, Node& parent) {
        File file(fileName, folder);
		int64 lastEdit = file.modifiedTime();
		log(parent.name, fileName);
		for(TextData s = file.read(file.size()); s; s.line()) {
			{String name = tryParseIncludes(s, fileName);
				if(name) {
					log("include", name);
					String module = find(name+".cc");
					if(!parent.edges.contains(module)) {
						String header = find(name+".h");
						if(header) lastEdit = max(lastEdit, parse(header+".h", parent));
						if(!module || parent == module) continue;
						if(!modules.contains(module)) { if(!compileModule(module)) return 0; }
						parent.edges.append( modules[modules.indexOf(module)].pointer );
					}
                }
            }
            //tryParseDefines(s);
            tryParseConditions(s, fileName);
            do { s.whileAny(" "); } while(tryParseFiles(s));
        }
        return lastEdit;
    }

    /// Compiles a module and its dependencies as needed
    /// \return Timestamp of the last modified module implementation (deep)
	bool compileModule(string target) {
        assert_(target);
		modules.append( unique<Node>(copyRef(target)) );
        Node& module = modules.last();
        String fileName = target+".cc";
        int64 lastEdit = parse(fileName, module);
		if(!lastEdit) return false;
        String object = tmp+"/"+join(flags,"-")+"/"+target+".o";
        if(!existsFile(object, folder) || lastEdit >= File(object).modifiedTime()) {
            while(pids.size>=4) { // Waits for a job to finish before launching a new unit
                int pid = wait(); // Waits for any child to terminate
				if(wait(pid)) { log("Failed to compile"); return false; }
                pids.remove(pid);
            }
            Folder(tmp+"/"+join(flags,"-")+"/"+section(target,'/',0,-2), root(), true);
            log(target);
			pids.append( execute(CXX, ref<string>{"-c", "-pipe", "-std=c++1y", "-Wall", "-Wextra", "-Wno-overloaded-virtual", //"-fno-rtti",
                                                  "-march=native", "-o" , object, fileName, "-I/usr/include/freetype2"} + toRefs(args), false) );
            needLink = true;
        }
        files.append( tmp+"/"+join(flags,"-")+"/"+target+".o" );
		return true;
    }

    Build() {
		/*if(arguments()==ref<string>{"statistics"}) {
            map<size_t, string> files;
			for(string path: filter(sources, [](string name) { return !(endsWith(name, ".cc")||endsWith(name,".h")); }))
				files.insertSortedMulti(File(path).size(), path);
			//log(str(files,"\n"_));
            return;
		}*/

        // Configures
        string install;
        for(string arg: arguments()) {
			if(startsWith(arg,"-")) {} // Build command flag
			else if(startsWith(arg,"/")) install=arg;
            else if(find(arg+".cc")) {
                if(target) log("Multiple targets unsupported, building last target:",arg);
				target = arg;
			}
			else flags.append( split(arg,"-") );
        }

		if(!target) target = base;
		assert_(find(target+".cc"), "Invalid target"_, target);

		args.append("-iquote."__);
        for(string flag: flags) args.append( "-D"+toUpper(flag)+"=1" );
		if(!flags.contains("release")) args.append("-g"__);
		if(!flags.contains("debug")) args.append("-O3"__);
		else if(flags.contains("fast")) args.append("-O1"__); // fast-debug
		if(flags.contains("profile")) args.append("-finstrument-functions"__);

        Folder(tmp, root(), true);
        Folder(tmp+"/"+join(flags,"-"), root(), true);

        // Compiles
		if(flags.contains("profile")) if(!compileModule(find("profile.cc"))) { log("Failed to compile"); requestTermination(-1); return; }
		if(!compileModule( find(target+".cc") )) { log("Failed to compile"); requestTermination(-1); return; }

		if(arguments().contains("-tree")) log(collect(modules.first(), 1)); return;

        // Links
		String binary = tmp+"/"+join(flags,"-")+"/"+target;
		assert_(!existsFolder(binary));
        if(!existsFile(binary) || needLink) {
            array<String> args = move(files);
			args.append("-o"__);
            args.append(copy(binary));
			args.append(apply(libraries, [this](const String& library)->String{ return "-l"+library; }));
			// Waits for all translation units to finish compilation before final link
			for(int pid: pids) if(wait(pid)) { log("Failed to compile"); requestTermination(-1); return; }
			if(execute(CXX, toRefs(args))) { log("Failed to link"); requestTermination(-1); return; }
        }

        // Installs
        if(install && (!existsFile(target, install) || File(binary).modifiedTime() > File(target, install).modifiedTime())) copy(root(), binary, install, target);
    }
} build;
