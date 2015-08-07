/// \file build.cc Builds C++ projects with automatic module dependency resolution
#include "build.h"
#include "data.h"

bool operator ==(const Node* a, const string b) { return a->name==b; }
bool operator ==(const Node& a, const string b) { return a.name==b; }
String str(const Node& o) {
	return o.name+'\n'+join(apply(o.edges, [=](const Node* child){ return str(child); }));
};

struct Branch {
	String name;
	array<Branch> children;
	explicit Branch(String&& name) : name(move(name)) {}
};
String str(const Branch& o, int depth=0) {
	return repeat(" ", depth)+o.name+'\n'+join(apply(o.children, [=](const Branch& child){ return str(child, depth+1); }));
};

/*/// Breaks cycles (converts a directed graph to a directed acyclic graph (DAG))
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
Branch collect(const Node& source, int maxDepth) { array<const Node*> stack; return collect(&source, stack, maxDepth); }*/

String Build::find(string file) {
	for(string path: sources) if(path == file) return copyRef(path.contains('.') ? section(path,'.',0,-2) : path); // Exact match
	for(string path: sources) if(section(path,'/',-2,-1) == file) return copyRef(path.contains('.') ? section(path,'.',0,-2) : path); // Sub match
	return {};
}

String Build::tryParseIncludes(TextData& s, string fileName) {
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
				string library=s.identifier("_-");
				if(!library) break;
				if(!libraries.contains(library)) libraries.append(copyRef(library));
			}
			assert_(s.wouldMatch('\n'));
		}
	}
	return {};
}

void Build::tryParseDefines(TextData& s) {
	if(!s.match("#define ")) return;
	string id = s.identifier("_");
	s.whileAny(" ");
	if(!s.match('0')) { defines.append( toLower(id) ); }
}

bool Build::tryParseConditions(TextData& s, string fileName) {
	if(!s.match("#if ")) return false;
	bool condition = !s.match('!');
	string id = s.identifier("_");
	bool value = false;
	/**/  if(id=="0") value=false;
	else if(id=="1") value=true;
	else if(flags.contains(toLower(id))) value=true; // Conditionnal build (extern use flag)
	else if(defines.contains(toLower(id))) value=true; // Conditionnal build (intern use flag)
	if(value != condition) {
  s.whileAny(" ");
		while(!s.match("#else") && !s.match("#endif")) {
			assert_(s, fileName+": Expected #endif, got EOD");
			if(!tryParseConditions(s, fileName)) s.line();
		}
	}
	return true;
}

bool Build::tryParseFiles(TextData& s) {
	if(!s.match("FILE(") && !s.match("ICON(")) return false;
 String name = copyRef(s.identifier("_-"));
	s.skip(')');
 name = replace(name, '_', '.');
 String path = find(name);
 /*String path;
 for(string p: sources) {
  if(section(p.contains('.')?section(p,'.'):p,'/',-2,-1) == name)
   { path=copyRef(p); break; }
 }*/
	assert(path, "No such file to embed", name);
 String filesPath = tmp+"/files";
 Folder(filesPath, currentWorkingDirectory(), true);
	Folder subfolder = Folder(section(path,'/',0,-2), folder);
 string file = name; //section(path,'/',-2,-1);
 String object = filesPath+"/"+name+".o";
	assert_(!files.contains(object), name);
 assert_(existsFile(file, subfolder), file, name);
	int64 lastFileEdit = File(file, subfolder).modifiedTime();
	if(!existsFile(object) || lastFileEdit >= File(object).modifiedTime()) {
		if(execute(LD, {"-r", "-b", "binary", "-o", object, file}, true, subfolder)) error("Failed to embed");
		needLink = true;
	}
	files.append( move(object) );
	return true;
}

int64 Build::parse(string fileName, Node& parent) {
	File file(fileName, folder);
	int64 lastEdit = file.modifiedTime();
	for(TextData s = file.read(file.size()); s; s.line()) {
		{String name = tryParseIncludes(s, fileName);
			if(name) {
				String module = find(name+".h"); // .h to find module corresponding to header
                assert_(module, "No such module", name, "imported from", fileName);
				lastEdit = max(lastEdit, parse(module+".h", parent));
				if(!parent.edges.contains(module) && existsFile(module+".cc", folder) && module != parent.name) {
					if(!modules.contains<string>(module)) { if(!compileModule(module)) return 0; }
					parent.edges.append( modules[modules.indexOf<string>(module)].pointer );
				}
			}
		}
  tryParseDefines(s);
		tryParseConditions(s, fileName);
		do { s.whileAny(" "); } while(tryParseFiles(s));
	}
	return lastEdit;
}

bool Build::compileModule(string target) {
	assert_(target);
	modules.append( unique<Node>(copyRef(target)) );
	Node& module = modules.last();
	String fileName = target+".cc";
	int64 lastEdit = parse(fileName, module);
	if(!lastEdit) return false;
	String object = tmp+"/"+join(flags,"-")+"/"+target+".o";
	if(!existsFile(object, folder) || lastEdit >= File(object).modifiedTime()) {
		while(jobs.size>=2) { // Waits for a job to finish before launching a new unit
			int pid = wait(); // Waits for any child to terminate
			int status = wait(pid);
			Job job = jobs.take(jobs.indexOf(pid));
			log(job.stdout.readUpTo(4096));
			if(status) { log("Failed to compile\n"); return false; }
			else log(job.target+'\n');
		}
		Folder(tmp+"/"+join(flags,"-")+"/"+section(target,'/',0,-2), currentWorkingDirectory(), true);
		Stream stdout;
  int pid = execute(CXX, ref<string>{"-c", "-pipe", "-std=c++14", "-Wall", "-Wextra", "-Wno-overloaded-virtual", "-Wno-strict-aliasing", "-march=native",
                                     "-o", object, fileName, "-I/usr/include/freetype2","-I/var/tmp/include"} + toRefs(args),
                    false, currentWorkingDirectory(), 0, &stdout);
		jobs.append({copyRef(target), pid, move(stdout)});
		needLink = true;
	}
	files.append( tmp+"/"+join(flags,"-")+"/"+target+".o" );
	return true;
}

Build::Build(ref<string> arguments, function<void(string)> log) : log(log) {
	// Configures
	string install;
	for(string arg: arguments) {
		if(startsWith(arg,"-"_)) {} // Build command flag
		else if(startsWith(arg,"/"_)) install=arg;
		else if(find(arg+".cc") && arg!="profile") {
			if(target) log(str("Multiple targets unsupported, building last target:", arg, ". Parsing arguments:", arguments)+'\n');
			target = arg;
		}
		else flags.append( split(arg,"-") );
	}

	if(!target) target = base;
	assert_(find(target+".cc"), "Invalid target"_, target, sources);

	args.append("-iquote."__);
	for(string flag: flags) args.append( "-D"+toUpper(flag)+"=1" );
	if(!flags.contains("release")) args.append("-g"__);
	if(!flags.contains("debug")) args.append("-O3"__);
 else if(flags.contains("fast")) args.append("-O3"__); // fast-debug
	if(flags.contains("profile")) args.append("-finstrument-functions"__);
	if(flags.contains("m32"_)) args.append("-m32"__); // "-mfpmath=sse"__

	Folder(tmp, currentWorkingDirectory(), true);
	Folder(tmp+"/"+join(flags,"-"), currentWorkingDirectory(), true);

	// Compiles
	if(flags.contains("profile")) if(!compileModule(find("core/profile.cc"))) { log("Failed to compile\n"); return; }
	if(!compileModule( find(target+".cc") )) { log("Failed to compile\n"); return; }

	//if(arguments.contains("-tree")) { log(str(collect(modules.first(), 1))+'\n'); return; }

	// Links
	binary = tmp+"/"+join(flags,"-")+"/"+target;
	assert_(!existsFolder(binary));
	if(!existsFile(binary) || needLink) {
		// Waits for all translation units to finish compilation before final link
		for(Build::Job& job: jobs) {
			int status = wait(job.pid);
			log(job.stdout.readUpTo(4096));
			if(status) { binary={}; return; }
			else log(job.target+'\n');
  }
  libraries.append("stdc++"__);
  libraries.append("m"__);
		array<String> args = (buffer<String>)(
					move(files) +
                    mref<String>{"-o"__, unsafeRef(binary), "-L/var/tmp/lib"__, "-Wl,-rpath,/var/tmp/lib"__,} +
					apply(libraries, [this](const String& library)->String{ return "-l"+library; }) );
		if(flags.contains("m32"_)) args.append("-m32"__);
  if(execute(CXX, toRefs(args))) { ::log("Failed to link\n", CXX, args); return; }
	}

	// Installs
	if(install && (!existsFile(target, install) || File(binary).modifiedTime() > File(target, install).modifiedTime())) copy(currentWorkingDirectory(), binary, install, target);
}

Build build {arguments()};
