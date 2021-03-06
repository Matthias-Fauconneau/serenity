#pragma once
#include "thread.h"
struct TextData;

struct Build {
	// Parameters
 String CXX {
  which(environmentVariable("CC")) ? which(environmentVariable("CC")) :
     which("clang++") ? which("clang++") : which("g++")};
 String LD {which("ld")};

 const Folder folder {"."};
 const String base {copyRef(section(folder.name(),'/',-2,-1))};
 const String tmp = "/var/tmp/"+base+"."+section(CXX,'/',-2,-1);

 string target;
 array<string> flags;
 array<String> args;
 array<String> linkArgs;

 function<void(string)> log;

 // Variables
 map<String, int64> lastEdit;
 array<String> defines;
 array<String> units; // to be compiled
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

 const buffer<String> sources = folder.list(Files|Recursive);

 /// Returns the first path matching file
 String find(string file);

 String tryParseIncludes(TextData& s, string fileName);
 void tryParseDefines(TextData& s);
 bool tryParseConditions(TextData& s, string fileName);
 bool tryParseFiles(TextData& s);

 /// Returns timestamp of the last modified interface header recursively parsing includes
 int64 parse(string fileName);

 /// Compiles a module and its dependencies as needed
 /// \return Timestamp of the last modified module implementation (deep)
 bool compileModule(string target);

 Build(ref<string> arguments, function<void(string)> log=log_);
};
