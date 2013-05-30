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
inline string str(const Rule& rule) { return str(rule.outputs,"=",rule.operation,rule.outputs,rule.arguments); }

/// Manages a process defined a direct acyclic graph of production rules
struct Process {
    Process(const ref<byte>& definition, const ref<ref<byte>>& arguments);

    /// Returns the Rule to evaluate in order to produce \a target
    Rule& ruleForOutput(const ref<byte>& target);

    /// Returns recursively relevant arguments for a rule
    Dict relevantArguments(const Rule& rule, const Dict& arguments);

    /// Recursively verifies \a target output is the same since \a queryTime
    bool sameSince(const ref<byte>& target, long queryTime, const Dict& arguments);

    /// Computes target volume
    virtual shared<Result> getVolume(const ref<byte>& target, const Dict& arguments);

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
    ResultFile(const ref<byte>& name, long timestamp, const ref<byte>& parameters, const ref<byte>& metadata, const Folder& folder, Map&& map, const ref<byte>& path)
        : Result(name,timestamp,parameters,metadata, buffer<byte>(map.data, map.size)), folder(folder), map(move(map)), fileName(path?:name+"."_+parameters+"."_+metadata+".1"_) {}
    void rename() {
        if(!fileName) return;
        string newName = name+"."_+arguments+"."_+metadata+"."_+str(userCount);
        if(fileName!=newName) { ::rename(fileName, newName, folder); fileName=move(newName); }
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

    /// Computes target volume
    shared<Result> getVolume(const ref<byte>& target, const Dict& arguments) override;

    Folder baseStorageFolder = "dev/shm"_; // Should be a RAM (or local disk) filesystem large enough to hold up to 2 intermediate operations of volume data (up to 32bit per sample)
    ref<byte> name; // Used to name intermediate and output files (folder base name)
    Folder storageFolder = ""_; // Holds intermediate operations data (=baseStorageFolder/name)
};
