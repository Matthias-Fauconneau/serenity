#pragma once
#include "operation.h"
#include "file.h"

/// Defines arguments taking multiple values
typedef map<string, array<Variant>> Sweeps;

/// Defines a production rule to evaluate outputs using an operation and any associated arguments
struct Rule {
    string operation;
    array<string> inputs;
    array<string> outputs;
    struct Expression : Variant { enum EType { Literal, Value } type=Value; Expression(Variant&& value=Variant(), EType type=Literal):Variant(move(value)),type(type){} };
    map<String, Expression> argumentExps;
    Sweeps sweeps; // Process-specified parameter sweeps
};
template<> inline String str(const Rule::Expression& e) { return str<Variant>(e); }
template<> inline String str(const Rule& rule) { return str(rule.outputs,"=",rule.operation,rule.inputs/*,rule.argumentExps?str(rule.argumentExps):""_,rule.sweeps?str(rule.sweeps):""_*/); }

/// Manages a process defined a direct acyclic graph of production rules
struct Process {
    /// Returns all valid parameters
    array<string> parameters();

    /// Configures process using given arguments and definition (which can depends on the arguments)
    array<string> configure(const ref<string>& allArguments, const string& definition);

    /// Parses special arguments
    virtual void parseSpecialArguments(const ref<string>& arguments) { assert_(!arguments); }

    /// Returns the Rule to evaluate in order to produce \a target
    Rule& ruleForOutput(const string& target);

    /// Recursively evaluates relevant arguments for a rule
    Dict evaluateArguments(const string& target, const Dict& arguments, bool local=false,  const string& scope=""_);

    /// Returns a cached Result for \a target with \a arguments (without checking validity)
    int indexOf(const string& target, const Dict& arguments);
    /// Returns a cached Result for \a target with \a arguments (without checking validity)
    const shared<Result>& find(const string& target, const Dict& arguments);

    /// Recursively verifies \a target output is the same since \a queryTime
    bool sameSince(const string& target, int64 queryTime, const Dict& arguments);

    /// Returns a valid cached Result for \a target with \a arguments or generates it if necessary
    virtual shared<Result> getResult(const string& target, const Dict& arguments);

    /// Recursively loop over each sweep parameters expliciting each value into arguments
    array<shared<Result>> execute(const string& target, const Sweeps& sweeps, const Dict& arguments);

    /// Executes all operations to generate all target (for each value of any parameter sweep) using given arguments and definition (which can depends on the arguments)
    void execute(const ref<string>& allArguments, const string& definition);

    array<string> specialParameters; // Valid parameters accepted for derived class special behavior
    Dict arguments; // User-specified arguments
    array<Sweeps> targetsSweeps; // User-specified parameter sweeps (for each target)
    array<Rule> rules; // Production rules
    array<string> resultNames; // Valid result names defined by process
    array<shared<Result>> results; // Generated intermediate (and target) data
    array<array<shared<Result>>> targetResults; // Generated data for each target
};

/// Mirrors results on a filesystem
struct ResultFile : Result {
    ResultFile(const string& name, long timestamp, Dict&& arguments, String&& metadata, String&& data, const string& path, const Folder& folder)
        : Result(name,timestamp,move(arguments),move(metadata), move(data)), fileName(String(path)), folder(folder) {}
    ResultFile(const string& name, long timestamp, Dict&& arguments, String&& metadata, Map&& map, const string& path, const Folder& folder)
        : Result(name,timestamp,move(arguments),move(metadata), buffer<byte>(map)), fileName(String(path)), folder(folder) { if(map) maps<<move(map); }
    void rename() {
        if(!fileName) return;
        String newName = name+"{"_+toASCII(relevantArguments)+"}"_+(userCount?str(userCount):String())+"."_+metadata;
        if(fileName!=newName) { ::rename(fileName, newName, folder); fileName=move(newName); }
    }
    void addUser() override { ++userCount; rename(); }
    uint removeUser() override { --userCount; rename(); return userCount; }

    array<Map> maps;
    String fileName;
    const Folder& folder;
};

/// Mirrors a process intermediate data on the filesystem for persistence and operations using multiple processes
struct PersistentProcess : virtual Process {
     PersistentProcess(const ref<byte>& name) : storageFolder(name,Folder("dev/shm"_),true) {}
    ~PersistentProcess();

    void parseSpecialArguments(const ref<string>& arguments) override;

    /// Gets result from cache or computes if necessary
    shared<Result> getResult(const string& target, const Dict& arguments) override;

    Folder storageFolder; // Should be a RAM (or local disk) filesystem large enough to hold intermediate operations of volume data
};
