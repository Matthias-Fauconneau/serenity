#pragma once
#include "thread.h"
struct TextData;

struct Node {
	String name;
	array<Node*> edges;
	explicit Node(String&& name) : name(move(name)){}
};

struct Build {
	// Parameters
	String CXX = which(getenv("CC")) ? which(getenv("CC")) : which("clang++") ? which("clang++") : which("g++");
	String LD = which("ld");

	const Folder folder {"."};
	const String base = copyRef(section(folder.name(),'/',-2,-1));
	const String tmp = "/var/tmp/"+base+"."+section(CXX,'/',-2,-1);

	string target;
	array<string> flags;
	array<String> args = apply(folder.list(Folders), [this](string subfolder)->String{ return "-iquote"+subfolder; });

	function<void(string)> log;

	// Variables
	array<String> defines;
	array<unique<Node>> modules;
	array<String> files;
	array<String> libraries;
	struct Process {
		int pid; Stream stdout;
		bool operator==(int pid) const { return pid=this->pid; }
	};
	array<Process> jobs;
	bool needLink = false;
	String binary;

	const array<String> sources = folder.list(Files|Recursive);
	/// Returns the first path matching file
	String find(string file);

	String tryParseIncludes(TextData& s, string fileName);
	void tryParseDefines(TextData& s);
	bool tryParseConditions(TextData& s, string fileName);
	bool tryParseFiles(TextData& s);

	/// Returns timestamp of the last modified interface header recursively parsing includes
	int64 parse(string fileName, Node& parent);

	/// Compiles a module and its dependencies as needed
	/// \return Timestamp of the last modified module implementation (deep)
	bool compileModule(string target);

	Build(ref<string> arguments, function<void(string)> log=log_);
};
