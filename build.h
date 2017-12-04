#pragma once
#include "thread.h"

struct Build {
    // Parameters
    const Folder folder {"."};
    const buffer<String> sources = folder.list(Files|Recursive);
    const String baseName { copyRef(section(folder.name(),'/',-2,-1)) };
    const String CXX = which(environmentVariable("CC")) ? which(environmentVariable("CC")) : which("clang++") ? which("clang++") : which("g++");
    const String LD {which("ld")};
    const String tmp = environmentVariable("XDG_RUNTIME_DIR")+"/"+baseName+"."+section(CXX,'/',-2,-1);

    string target;
    array<string> flags;
    array<String> defines;
    array<String> args;
    array<String> linkArgs;

    function<void(string)> log;

    // Variables
    map<String, int64> lastEdit;
    array<String> units;
    array<String> modules;
    array<String> files;
    array<String> libraries;
    struct Job {
        String target;
        int pid;
        Stream stdout;
        bool operator==(int pid) const { return pid=this->pid; }
    };
    array<Job> jobs;
    bool needLink = false;
    String binary;

    /// Returns the first path matching file
    String find(string file);

    /// Returns timestamp of the last modified interface header recursively parsing includes
    int64 parse(string fileName);

    /// Compiles a module and its dependencies as needed
    /// \return Timestamp of the last modified module implementation (deep)
    bool compileModule(string target);

    Build(ref<string> arguments, function<void(string)> log=log_);
};
