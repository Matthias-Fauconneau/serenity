/// \file build.cc Builds C++ projects with automatic module dependency resolution
#include "build.h"
#include "data.h"
#include "time.h"

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
        if(!s.match("h\"\n")) error(fileName, "Expected '"+escape("h\"\n")+"', got '"+escape(s.peek("h\"\n"_.size))+'\'', s.line());
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

int64 Build::parse(string fileName) {
    int64 lastEdit = this->lastEdit.value(fileName);
    if(lastEdit) return lastEdit;
    Time time {true};
    File file(fileName, folder);
    lastEdit = file.modifiedTime();
    TextData s = file.read(file.size());
    s.match("#pragma once");
    for(;;) {
        s.whileAny(" \t\r\n");
        if(s.match("//")) { s.line(); continue; }
        String name = tryParseIncludes(s, fileName);
        if(!name) { /*log(s.line()+"\n");*/ break; }
        String module = find(name+".h") ? find(name+".h") : find(name+".cc");
        assert_(module, "No such module", name, "imported from", fileName);

        if(existsFile(module+".h")) {
            assert_(String(module+".h") != fileName);
            lastEdit = max(lastEdit, parse(module+".h"));
        }
        if(existsFile(module+".cc", folder) && !modules.contains(module)) units.add(::move(module));
    }
    this->lastEdit.insert(copyRef(fileName), lastEdit);
    if(time.seconds()>0.5) log(str(fileName,time)+'\n');
    return lastEdit;
}

bool Build::compileModule(string target) {
    assert_(target);
    modules.append(copyRef(target));
    String fileName = target+".cc";
    int64 lastEdit = parse(fileName);
    if(!lastEdit) return false;
    String object = tmp+"/"+join(flags,string("-"_))+"/"+target+".o";
    if(!existsFile(object, folder) || lastEdit >= File(object).modifiedTime()) {
        while(jobs.size>=1) { // Waits for a job to finish before launching a new unit
            int pid = wait(); // Waits for any child to terminate
            int status = wait(pid);
            Job job = jobs.take(jobs.indexOf(pid));
            log(job.stdout.readUpTo(65536));
            if(status) { log("Failed to compile\n"); return false; }
            else log(job.target+'\n');
        }
        Folder(tmp+"/"+join(flags,string("-"_))+"/"+section(target,'/',0,-2), currentWorkingDirectory(), true);
        Stream stdout;
        //::log(args);
        int pid = execute(CXX, ref<string>{"-c", "-pipe", "-std=c++1z",/*"-fno-exceptions",*/"-fno-operator-names", //"-stdlib=libc++", //"-mfp16-format",
                                           "-Wall", "-Wextra", "-Wno-overloaded-virtual", "-Wno-strict-aliasing",
                                           "-I/usr/include/freetype2","-I/usr/include/ffmpeg",
                                           "-I/var/tmp/include", "-iquote.",
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
    args.append( "-DARGS=\""+str(args)+"\"");
    args.append(apply(folder.list(Folders), [this](string subfolder)->String{ return "-iquote"+subfolder; }));
    for(string flag: flags) args.append( "-D"+toUpper(flag)+"=1" );
    Stream stdout;
    execute(which("git"), {"describe"_,"--long"_,"--tags"_,"--dirty"_,"--always"_}, true,
            currentWorkingDirectory(), &stdout);
    String version = simplify( stdout.readUpTo(64) );
    args.append("-DVERSION=\""_+version+"\""_);

    Folder(tmp, currentWorkingDirectory(), true);
    Folder(tmp + "/"_ + join(flags, "-"_), currentWorkingDirectory(), true);

    // Compiles
    if(flags.contains("profile")) if(!compileModule(find("core/profile.cc"))) { log("Failed to compile\n"); return; }
    if(!compileModule( find(target+".cc") )) { log("Failed to compile\n"); return; }
    while(units) compileModule( units.take(0) );

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
                                 ,"-stdlib=libc++"__, "-lcxxrt"__,"-lm"__
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
