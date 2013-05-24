#pragma once
#include "operation.h"
#include "file.h"

/// Defines a production rule to evaluate outputs using an operation and any associated arguments
struct Rule {
    ref<byte> operation;
    array<ref<byte>> inputs;
    array<ref<byte>> outputs;
    map<ref<byte>, Variant> arguments;
};
inline const ref<byte>& str(const Rule& rule) { return rule.operation; }

/// Manages a process defined a direct acyclic graph of production rules
struct Process {
    Process(const ref<byte>& definition, const ref<ref<byte>>& arguments);

    /// Returns the Rule to evaluate in order to produce \a target
    Rule* ruleForOutput(const ref<byte>& target);

    /// Recursively verifies \a target output is the same since \a queryTime
    bool sameSince(const ref<byte>& target, long queryTime);

    /// Computes target volume
    virtual shared<Result> getVolume(const ref<byte>& target);

    array<Rule> rules; // Production rules
    array<ref<byte>> targets; // Target results to compute
    map<ref<byte>, Variant> arguments; // User given arguments
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
    shared<Result> getVolume(const ref<byte>& target) override;

    Folder baseStorageFolder = "dev/shm"_; // Should be a RAM (or local disk) filesystem large enough to hold up to 2 intermediate operations of volume data (up to 32bit per sample)
    ref<byte> name; // Used to name intermediate and output files (folder base name)
    Folder storageFolder = ""_; // Holds intermediate operations data (=baseStorageFolder/name)
};
