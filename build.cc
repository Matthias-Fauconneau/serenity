/// \file build.cc Builds C++ projects with automatic module dependency resolution
#include "build.h"
#include "data.h"

String Build::find(string file) {
    for(string path: sources)
        if(path == file)
            return copyRef(path.contains('.') ? section(path,'.',0,-2) : path); // Exact match
    for(string path: sources)
        if(section(path,'/',-2,-1) == file)
            return copyRef(path.contains('.') ? section(path,'.',0,-2) : path); // Sub match
    return {};
}

int64 Build::parse(string fileName) {
    int64 lastEdit = this->lastEdit.value(fileName);
    if(lastEdit) return lastEdit;
    File file(fileName, folder);
    lastEdit = file.modifiedTime();
    TextData s = file.read(file.size());
    s.match("\xEF\xBB\xBF");
    if(endsWith(fileName,".h")) assert_(s.match("#pragma once"), fileName);
    else assert_(!s.match("#pragma once"), fileName);
    for(;;) {
        s.whileAny(" \t\r\n");
        /**/ if(s.match("#include \"") || s.match("//include \"")) { // Module header
            string name = s.until('.');
            if(!s.match("h\"\n")) error(fileName, "Expected '"+escape("h\"\n")+"', got '"+escape(s.peek("h\"\n"_.size))+'\'', s.line());
            String module;
            if(fileName.contains('/'))
                module = find(section(fileName,'/')+'/'+name+".h") ? find(section(fileName,'/')+'/'+name+".h") : find(section(fileName,'/')+'/'+name+".cc");
            if(!module) module = find(name+".h") ? find(name+".h") : find(name+".cc");
            assert_(module, "No such module", name, "imported from", fileName);
            if(existsFile(module+".h")) {
                assert_(String(module+".h") != fileName);
                lastEdit = max(lastEdit, parse(module+".h"));
            }
            if(existsFile(module+".cc", folder) && !modules.contains(module)) units.add(::move(module));
        }
        else if(s.match("#include <")) { // Library header
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
        else if(s.match("#if ")) { // #if
            const string id = s.whileNot('\n');
            if(id!="1" && !flags.contains(toLower(id)) && !defines.contains(id)) s.until("#endif");
        }
        else if(s.match("FILE(")) { // FILE()
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
                //else log(file);
                needLink = true;
            }
            files.append( move(object) );
        }
        else if(s.match("#define ")) { // #define
            const string id = s.identifier("_");
            s.whileAny(" ");
            if(!s.match('0')) defines.append(copyRef(id));
            s.match('1') || s.identifier("_");
            s.skip('\n');
        }
        else if(s.match("//") || s.match("extern \"C\" {")) {
            s.line(); continue;
        }
        else {
            assert_(!s.match("#pragma once"));
            break;
        }
    }
    this->lastEdit.insert(copyRef(fileName), lastEdit);
    return lastEdit;
}

bool Build::compileModule(string target) {
    assert_(target);
    modules.append(copyRef(target));
    String fileName = target+".cc";
    int64 lastEdit = parse(fileName);
    while(units) compileModule( units.take(0) ); // Always compile dependencies before to preserve static constructor order
    if(!lastEdit) return false;
    String object = tmp+"/"+join(flags,string("-"_))+"/"+target+".o";
    if(!existsFile(object, folder) || lastEdit >= File(object).modifiedTime()) {
        while(jobs.size>=8) { // Waits for a job to finish before launching a new unit
            int pid = wait(); // Waits for any child to terminate
            int status = wait(pid);
            Job job = jobs.take(jobs.indexOf(pid));
            log(job.stdout.readUpTo(65536));
            if(status) { log("Failed to compile\n"); return false; }
            else log(job.target+'\n');
        }
        Folder(tmp+"/"+join(flags,string("-"_))+"/"+section(target,'/',0,-2), currentWorkingDirectory(), true);
        Stream stdout;
        int pid = execute(CXX, ref<string>{"-c", "-pipe", "-std=c++1z","-fno-operator-names", //"-stdlib=libc++",
                                           "-Wall", "-Wextra", "-Wno-overloaded-virtual", "-Wno-strict-aliasing",
                                           "-I/usr/include/freetype2","-I/usr/include/ffmpeg", "-iquote.",
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
        else if(startsWith(arg,"-"_)) {
            args.append(copyRef(arg));
            linkArgs.append(copyRef(arg));
        }
        else flags.append( split(arg,"-") );
    }

    if(!target) target = baseName;
    assert_(find(target+".cc"), "Invalid target"_, target, sources);

    args.append("-g"__);
    if(flags.contains("profile")) args.append("-finstrument-functions"__);
    for(string flag: flags) {
        if(flag=="native"_||flag=="haswell"_||flag=="core_avx2"_||flag=="sandybridge"_||flag=="core_avx_i"_)
            args.append("-march="+replace(flag,'_','-'));
    }
    if(flags.contains("mic"_)) {
        args.append("-mmic"__);
        args.append("-ansi-alias"__);
        linkArgs.append("-mmic"__);
    }
    if(flags.contains("openmp"_)) { args.append("-fopenmp"__); linkArgs.append("-fopenmp"__); }
    if(flags.contains("pg"_)) {
        args.append("-fprofile-generate"__);
        linkArgs.append("-fprofile-generate"__);
    }
    if(flags.contains("pgu"_)) {
        args.append("-fprofile-use"__);
        linkArgs.append("-fprofile-use"__);
    }
    if(flags.contains("asan"_)) {
        args.append("-fsanitize=address"__);
        linkArgs.append("-fsanitize=address"__);
    }
    for(string flag: flags) if(startsWith(flag,"O")) args.append("-"+flag);
    //args.append( "-DARGS=\""+str(args)+"\"");
    args.append(apply(folder.list(Folders), [this](string subfolder)->String{ return "-iquote"+subfolder; }));
    for(string flag: flags) args.append( "-D"+toUpper(flag)+"=1" );
    /*Stream stdout;
    execute(which("git"), {"describe"_,"--long"_,"--tags"_,"--dirty"_,"--always"_}, true,
            currentWorkingDirectory(), &stdout);
    String version = simplify( stdout.readUpTo(64) );
    args.append("-DVERSION=\""_+version+"\""_);*/

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
        array<String> args = (buffer<String>)(
                    move(files) +
                    mref<String>{"-o"__, unsafeRef(binary)} + // , "-stdlib=libc++"__
                    apply(libraries, [this](const String& library)->String{ return "-l"+library; }) );
        if(execute(CXX, toRefs(args)+toRefs(linkArgs))) { ::log("Failed to link\n", CXX, args); return; }
        //else log(target);
    }

    // Installs
    if(install && (!existsFile(target, install) || File(binary).modifiedTime() > File(target, install).modifiedTime())) {
        ::log(binary, "->", install+"/"+target);
        copy(currentWorkingDirectory(), binary, install, target);
    }

    // Reports unused files
    //for(string file: Folder("app"_,folder).list(Files)) if(endsWith(file, ".cc")) compileModule( find(file) );
    //for(string file: sources) if((endsWith(file,".h")||endsWith(file,".cc")) && !startsWith(file,"build.") && !lastEdit.keys.contains(file)) ::log(file);
}
static Build build {arguments()};
