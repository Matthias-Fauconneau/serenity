#pragma once
#include "operation.h"
#include "file.h"

/// Defines a production rule to evaluate outputs using an operation and any associated arguments
struct Rule {
    ref<byte> operation;
    array<ref<byte>> inputs;
    array<ref<byte>> outputs;
    Dict arguments;
};
inline string str(const Rule& rule) { return str(rule.outputs,"=",rule.operation,rule.inputs,rule.arguments?str(rule.arguments):""_); }

/// Manages a process defined a direct acyclic graph of production rules
struct Process {
    /// Parses process definition (which can depends on explicit arguments)
    void parseDefinition(const ref<byte>& definition, int pass);

    /// Parses special arguments
    virtual void parseSpecialArguments(const ref<ref<byte>>& arguments) { assert_(!arguments); }

    /// Returns the Rule to evaluate in order to produce \a target
    Rule& ruleForOutput(const ref<byte>& target);

    /// Returns recursively relevant arguments for a rule
    Dict relevantArguments(const Rule& rule, const Dict& arguments);

    /// Returns a cached Result for \a target with \a arguments (without checking validity)
    int indexOf(const ref<byte>& target, const Dict& arguments);
    /// Returns a cached Result for \a target with \a arguments (without checking validity)
    const shared<Result>& find(const ref<byte>& target, const Dict& arguments);

    /// Recursively verifies \a target output is the same since \a queryTime
    bool sameSince(const ref<byte>& target, int64 queryTime, const Dict& arguments);

    /// Returns a valid cached Result for \a target with \a arguments or generates it if necessary
    virtual shared<Result> getResult(const ref<byte>& target, const Dict& arguments);

    /// Recursively loop over each sweep parameters expliciting each value into arguments
    void execute(const array<ref<byte> >& targets, const map<ref<byte>, array<Variant>>& sweeps, const Dict& arguments);

    /// Executes all operations to generate all target (for each value of any parameter sweep) using given arguments and definition (which can depends on the arguments)
    void execute(const ref<ref<byte> >& allArguments, const ref<byte>& definition);

    array<ref<byte>> parameters; // Valid parameters accepted by operations compiled in this binary and used in process definition
    Dict defaultArguments; // Application specific default arguments (defined by process definition)
    Dict arguments; // User-specified arguments
    map<ref<byte>, array<Variant>> sweeps; // User-specified parameter sweeps
    array<Rule> rules; // Production rules
    array<shared<Result>> results; // Generated intermediate (and target) data
    array<shared<Result>> targetResults; // Generated target data
};

/// Mirrors results on a filesystem
struct ResultFile : Result {
    ResultFile(const ref<byte>& name, long timestamp, Dict&& arguments, string&& metadata, string&& data, const ref<byte>& path, const Folder& folder)
        : Result(name,timestamp,move(arguments),move(metadata), move(data)), fileName(string(path)), folder(folder) {}
    ResultFile(const ref<byte>& name, long timestamp, Dict&& arguments, string&& metadata, Map&& map, const ref<byte>& path, const Folder& folder)
        : Result(name,timestamp,move(arguments),move(metadata), buffer<byte>(map)), fileName(string(path)), folder(folder) { if(map) maps<<move(map); }
    void rename() {
        if(!fileName) return;
        string newName = name+"{"_+toASCII(relevantArguments)+"}"_+(userCount?str(userCount):string())+"."_+metadata;
        if(fileName!=newName) { ::rename(fileName, newName, folder); fileName=move(newName); }
    }
    void addUser() override { ++userCount; rename(); }
    uint removeUser() override { --userCount; rename(); return userCount; }

    array<Map> maps;
    string fileName;
    const Folder& folder;
};

/// Mirrors a process intermediate data on the filesystem for persistence and operations using multiple processes
struct PersistentProcess : Process {
    ~PersistentProcess();

    void parseSpecialArguments(const ref<ref<byte>>& arguments) override;

    /// Gets result from cache or computes if necessary
    shared<Result> getResult(const ref<byte>& target, const Dict& arguments) override;

    Folder baseStorageFolder = "dev/shm"_; // Should be a RAM (or local disk) filesystem large enough to intermediate operations of volume data (e.g. up to 64bit per sample input and output)
    ref<byte> name; // Used to name intermediate and output files (folder base name)
    Folder storageFolder = ""_; // Holds intermediate operations data (=baseStorageFolder/name)
};
