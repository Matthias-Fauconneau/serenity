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
    Process(const ref<byte>& definition, const ref<ref<byte>>& arguments);

    /// Returns the Rule to evaluate in order to produce \a target
    Rule& ruleForOutput(const ref<byte>& target);

    /// Returns recursively relevant arguments for a rule
    Dict relevantArguments(const Rule& rule, const Dict& arguments);

    /// Returns a cached Result for \a target with \a arguments (without checking validity)
    int indexOf(const ref<byte>& target, const Dict& arguments);
    /// Returns a cached Result for \a target with \a arguments (without checking validity)
    const shared<Result>& find(const ref<byte>& target, const Dict& arguments);

    /// Recursively verifies \a target output is the same since \a queryTime
    bool sameSince(const ref<byte>& target, long queryTime, const Dict& arguments);

    /// Returns a valid cached Result for \a target with \a arguments or generates it if necessary
    virtual shared<Result> getResult(const ref<byte>& target, const Dict& arguments);

    /// Executes all operations to generate each target (for each value of any parameter sweep)
    void execute();
    /// Recursively loop over each sweep parameters expliciting each value into arguments
    void execute(const map<ref<byte>, array<Variant>>& sweeps, const Dict& arguments);

    array<Rule> rules; // Production rules
    array<ref<byte>> targets; // Target results to compute
    Dict defaultArguments; // Process specified default arguments
    Dict arguments; // User-specified arguments
    map<ref<byte>, array<Variant>> sweeps; // User-specified parameter sweeps
    array<shared<Result>> results; // Generated intermediate (and target) data
    array<shared<Result>> targetResults; // Generated target data
};

struct ResultFile : Result {
    ResultFile(const ref<byte>& name, long timestamp, Dict&& arguments, string&& metadata, const Folder& folder, Map&& map, const ref<byte>& path)
        : Result(name,timestamp,move(arguments),move(metadata), buffer<byte>(map.data, map.size)), folder(folder), map(move(map)), fileName(path?string(path):name+"."_+toASCII(arguments)+"."_+metadata+".1"_)
    { assert(fileName.capacity); }
    void rename() {
        if(!fileName) return;
        string newName = name+"{"_+toASCII(relevantArguments)+"}["_+metadata+"]("_+str(userCount)+")"_;
        if(fileName!=newName) { assert(fileName.capacity); ::rename(fileName, newName, folder); fileName=move(newName); }
    }
    void addUser() override { ++userCount; rename(); }
    uint removeUser() override { --userCount; rename(); return userCount; }

    const Folder& folder;
    Map map;
    string fileName;
};

/// Mirrors a process intermediate data on the filesystem for persistence and operations using multiple processes
struct PersistentProcess : Process {
    PersistentProcess(const ref<byte>& definition, const ref<ref<byte>>& arguments);
    ~PersistentProcess();

    /// Gets result from cache or computes if necessary
    shared<Result> getResult(const ref<byte>& target, const Dict& arguments) override;

    Folder baseStorageFolder = "dev/shm"_; // Should be a RAM (or local disk) filesystem large enough to hold up to 2 intermediate operations of volume data (up to 32bit per sample)
    ref<byte> name; // Used to name intermediate and output files (folder base name)
    Folder storageFolder = ""_; // Holds intermediate operations data (=baseStorageFolder/name)
};
