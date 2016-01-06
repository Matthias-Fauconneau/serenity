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

String Build::find(string file) {
 for(string path: sources)
  if(path == file)
   return copyRef(path.contains('.') ? section(path,'.',0,-2) : path); // Exact match
 for(string path: sources)
  if(section(path,'/',-2,-1) == file)
   return copyRef(path.contains('.') ? section(path,'.',0,-2) : path); // Sub match
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
    String module = find(name+".h") ? find(name+".h") : find(name+".cc");
    assert_(module, "No such module", name, "imported from", fileName);
    if(existsFile(module+".h")) lastEdit = max(lastEdit, parse(module+".h", parent));
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
 String object = tmp+"/"+join(flags,string("-"_))+"/"+target+".o";
 if(!existsFile(object, folder) || lastEdit >= File(object).modifiedTime()) {
  while(jobs.size>=1) { // Waits for a job to finish before launching a new unit
   int pid = wait(); // Waits for any child to terminate
   int status = wait(pid);
   Job job = jobs.take(jobs.indexOf(pid));
   log(job.stdout.readUpTo(32768));
   if(status) { log("Failed to compile\n"); return false; }
   else log(job.target+'\n');
  }
  Folder(tmp+"/"+join(flags,string("-"_))+"/"+section(target,'/',0,-2), currentWorkingDirectory(), true);
  Stream stdout;
  int pid = execute(CXX, ref<string>{"-c", "-pipe", "-std=c++11",
                                     "-Wall", "-Wextra", "-Wno-overloaded-virtual", "-Wno-strict-aliasing",
                                     "-I/usr/include/freetype2","-I/var/tmp/include", "-iquote.",
                                     "-o", object, fileName} + toRefs(args),
                    false, currentWorkingDirectory(), 0, &stdout);
  jobs.append(copyRef(target), pid, move(stdout));
  needLink = true;
 }
 files.append(tmp+"/"+join(flags,string("-"_))+"/"+target+".o");
 return true;
}

Build::Build(ref<string> arguments, function<void(string)> log) : log(log) {
 // Configures
 string install;
 for(string arg: arguments) {
  if(startsWith(arg,"/"_) || startsWith(arg,"~/"_)) install=arg;
  else if(find(arg+".cc") && arg!="profile") {
   if(target) log(str("Multiple targets unsupported, building last target:", arg, ". Parsing arguments:", arguments)+'\n');
   target = arg;
  }
  else if(startsWith(arg,"-"_) /*&& arg.contains('=')*/) {
   args.append(copyRef(arg));
   linkArgs.append(copyRef(arg));
  }
  //else if(startsWith(arg,"-"_)) {} // Build command flag
  else flags.append( split(arg,"-") );
 }

 if(!target) target = base;
 assert_(find(target+".cc"), "Invalid target"_, target, sources);

 //args.append("-iquote."__);
 args.append("-g"__);
 if(flags.contains("profile")) args.append("-finstrument-functions"__);
 for(string flag: flags) if(flag=="native"_||flag=="haswell"_||flag=="core_avx2"_||flag=="sandybridge"_)
  args.append("-march="+replace(flag,'_','-'));
 for(string flag: flags) if(flag=="mic"_) {
  //args.append("-xMIC-AVX512"__);
  args.append("-mmic"__);
  //args.append(ref<String>{"-opt"__, "-report-phase"__, "hlo"__,  "-opt-report"__, "3"__});
  args.append("-ansi-alias"__);
  //args.append("-no-opt-prefetch"__);
  linkArgs.append("-mmic"__);
 }
 for(string flag: flags) if(flag=="openmp"_) { args.append("-fopenmp"__); linkArgs.append("-fopenmp"__); }
 for(string flag: flags) if(startsWith(flag,"O")) args.append("-"+flag);
 args.append( "-DARGS=\""+str(args)+"\"");
 args.append(apply(folder.list(Folders), [this](string subfolder)->String{ return "-iquote"+subfolder; }));
 for(string flag: flags) args.append( "-D"+toUpper(flag)+"=1" );

 Folder(tmp, currentWorkingDirectory(), true);
 Folder(tmp + "/"_ + join(flags, "-"_), currentWorkingDirectory(), true);

 // Compiles
 if(flags.contains("profile")) if(!compileModule(find("core/profile.cc"))) { log("Failed to compile\n"); return; }
 if(!compileModule( find(target+".cc") )) { log("Failed to compile\n"); return; }

 // Links
 binary = tmp+"/"_+join(flags, "-"_)+"/"_+target;
 assert_(!existsFolder(binary));
 if(!existsFile(binary) || needLink) {
  // Waits for all translation units to finish compilation before final link
  for(Build::Job& job: jobs) {
   int status = wait(job.pid);
   log(job.stdout.readUpTo(65536));
   if(status) { binary={}; return; }
   else log(job.target+'\n');
  }
  //libraries.append("stdc++"__);
  //libraries.append("m"__);
  //libraries.append("c"__);
  array<String> args = (buffer<String>)(
     move(files) +
     mref<String>{"-o"__, unsafeRef(binary) /*, "-L/var/tmp/lib"__, "-Wl,-rpath,/var/tmp/lib"__*/
     /*,"-static-libstdc++"__*/} +
     apply(libraries, [this](const String& library)->String{ return "-l"+library; }) );
  if(execute(linkArgs.contains("-mmic")||1?CXX:LD, toRefs(args)+toRefs(linkArgs))) {
   ::log("Failed to link\n", linkArgs.contains("-mmic")||1?CXX:LD, args); return;
  }
 }

 // Installs
 if(install && (!existsFile(target, install) || File(binary).modifiedTime() > File(target, install).modifiedTime())) {
  ::log(binary, "->", install+"/"+target);
  copy(currentWorkingDirectory(), binary, install, target);
 }
}

Build build {arguments()};
