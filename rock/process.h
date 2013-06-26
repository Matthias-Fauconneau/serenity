#pragma once
#include "operation.h"
#include "file.h"

/// Defines a production rule to evaluate outputs using an operation and any associated arguments
struct Rule {
    string operation;
    array<string> inputs;
    array<string> outputs;
    map<String, Variant> arguments;
    array<string> parameters() const;
};
template<> inline String str(const Rule& rule) { return str(rule.outputs,"=",rule.operation,rule.inputs); }

/// Manages a process defined a direct acyclic graph of production rules
struct Process {
    /// Returns all valid parameters (accepted by operations compiled in this binary, used in process definition or for derived class special behavior)
    array<string> parameters();

    /// Configures process using given arguments and definition (which can depends on the arguments)
    virtual array<string> configure(const ref<string>& allArguments, const string& definition);

    /// Parses special arguments
    virtual void parseSpecialArguments(const ref<string>& arguments) { assert_(!arguments); }

    /// Returns the Rule to evaluate in order to produce \a target
    Rule& ruleForOutput(const string& target);

    /// Recursively evaluates relevant arguments for a rule
    const Dict& relevantArguments(const string& target, const Dict& arguments);

    /// Returns a cached Result for \a target with \a arguments (without checking validity)
    int indexOf(const string& target, const Dict& arguments);

    /// Returns a cached Result for \a target with \a arguments (without checking validity)
    const shared<Result>& find(const string& target, const Dict& arguments);

    /// Recursively verifies \a target output is the same since \a queryTime
    bool sameSince(const string& target, int64 queryTime, const Dict& arguments);

    /// Converts matching scoped arguments to local arguments for execution
    Dict localArguments(const string& target, const Dict& scopeArguments);

    /// Returns a valid cached Result for \a target with \a arguments or generates it if necessary
    virtual shared<Result> getResult(const string& target, const Dict& arguments) abstract;

    array<string> specialParameters; // Valid parameters accepted for derived class special behavior
    Dict arguments; // User-specified arguments
    array<Rule> rules; // Production rules
    array<string> resultNames; // Valid result names defined by process
    struct Evaluation { String target; Dict input; Dict output; Evaluation(String&& target, Dict&& input, Dict&& output) : target(move(target)), input(move(input)), output(move(output)){}};
    array<unique<Evaluation>> cache; // Caches argument evaluation
    array<shared<Result>> results; // Generated intermediate (and target) data
    array<shared<Result>> targetResults; // Generated datas for each target
};

/// Mirrors results on a filesystem
struct ResultFile : Result {
    ResultFile(const string& name, long timestamp, Dict&& arguments, String&& metadata, String&& data, const string& path, const Folder& folder)
        : Result(name,timestamp,move(arguments),move(metadata), move(data)), fileName(String(path)), folder(""_,folder) {}
    ResultFile(const string& name, long timestamp, Dict&& arguments, String&& metadata, Map&& map, const string& path, const Folder& folder)
        : Result(name,timestamp,move(arguments),move(metadata), buffer<byte>(map)), fileName(String(path)), folder(""_,folder) { if(map) maps<<move(map); }
    void rename() {
        if(!fileName) return;
        String newName = name+"{"_+toASCII(localArguments)+"}"_+(userCount?str(userCount):String())+"."_+metadata;
        if(fileName!=newName) { ::rename(fileName, newName, folder); fileName=move(newName); }
    }
    void addUser() override { ++userCount; rename(); }
    uint removeUser() override { --userCount; rename(); return userCount; }

    array<Map> maps;
    String fileName;
    Folder folder;
};

/// Mirrors a process intermediate data on the filesystem for persistence and operations using multiple processes
struct PersistentProcess : virtual Process {
     PersistentProcess(const ref<byte>& name) : storageFolder(name,Folder("dev/shm"_),true) { specialParameters += "storageFolder"_; }
    ~PersistentProcess();

    array<string> configure(const ref<string>& allArguments, const string& definition) override;

    /// Gets result from cache or computes if necessary
    shared<Result> getResult(const string& target, const Dict& arguments) override;

    Folder storageFolder; // Should be a RAM (or local disk) filesystem large enough to hold intermediate operations of volume data (TODO: rename to -> temporaryFolder ?)
};
