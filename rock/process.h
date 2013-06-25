#pragma once
#include "operation.h"
#include "file.h"

/// Defines arguments taking multiple values
typedef map<String, array<Variant>> Sweeps;

/// Defines a production rule to evaluate outputs using an operation and any associated arguments
struct Rule {
    string operation;
    array<string> inputs;
    array<string> outputs;
    struct Expression : Variant { enum EType { Literal, Value } type=Value; Expression(Variant&& value=Variant(), EType type=Literal):Variant(move(value)),type(type){} };
    map<String, Expression> argumentExps;
    Sweeps sweeps; // Process-specified parameter sweeps
    bool arrayOperation = false; // Run operation on each element of an input array
    array<string> parameters();
    Dict scope(const Dict& scope);
};
template<> inline String str(const Rule::Expression& e) { return str<Variant>(e); }
template<> inline String str(const Rule& rule) { return str(rule.outputs,"=",rule.operation,rule.inputs/*,rule.argumentExps,rule.sweeps*/)+(rule.arrayOperation?"[]"_:""_); }

/// Manages a process defined a direct acyclic graph of production rules
struct Process {
    /// Returns all valid parameters (accepted by operations compiled in this binary, used in process definition or for derived class special behavior)
    array<string> parameters();

    /// Configures process using given arguments and definition (which can depends on the arguments)
    virtual array<string> configure(const ref<string>& allArguments, const string& definition);

    /// Returns whether a parameter is defined (as an argument or (local/global) sweep)
    bool isDefined(const string& parameter);

    /// Parses special arguments
    virtual void parseSpecialArguments(const ref<string>& arguments) { assert_(!arguments); }

    /// Returns the Rule to evaluate in order to produce \a target
    Rule& ruleForOutput(const string& target);

    enum ArgFlags {
        None,
        Recursive=1<<0, // Includes recursively relevant arguments
        Sweep=1<<1, // Includes arguments handled by sweep generator rules
        Local=1<<2, // Includes arguments only used locally by the rule
        LocalSweep=1<<3, // Includes arguments handled by top sweep generator rule
        Cache=Recursive|Sweep|Local // Includes recursive relevance (in case input is not available), sweeps and locals
    };
    /// Recursively evaluates relevant arguments for a rule
    const Dict& evaluateArguments(const string& target, const Dict& arguments, ArgFlags flags=None, const string& scope=""_);

    /// Returns a cached Result for \a target with \a arguments (without checking validity)
    int indexOf(const string& target, const Dict& arguments);
    /// Returns a cached Result for \a target with \a arguments (without checking validity)
    const shared<Result>& find(const string& target, const Dict& arguments);

    /// Recursively verifies \a target output is the same since \a queryTime
    bool sameSince(const string& target, int64 queryTime, const Dict& arguments);

    /// Returns a valid cached Result for \a target with \a arguments or generates it if necessary
    virtual shared<Result> getResult(const string& target, const Dict& arguments, const string& scope=""_);

    /// Recursively loop over each sweep parameters expliciting each value into arguments
    array<shared<Result>> execute(const string& target, const Sweeps& sweeps, const Dict& arguments);

    /// Executes all operations to generate all target (for each value of any parameter sweep) using given arguments and definition (which can depends on the arguments)
    array<array<shared<Result>>> execute(const ref<string>& allArguments, const string& definition);

    array<string> specialParameters; // Valid parameters accepted for derived class special behavior
    Dict arguments; // User-specified arguments
    array<Sweeps> targetsSweeps; // User-specified parameter sweeps (for each target)
    array<Rule> rules; // Production rules
    array<string> resultNames; // Valid result names defined by process
    array<shared<Result>> results; // Generated intermediate (and target) data
    struct Evaluation {
        String target; Dict input; ArgFlags flags; Dict output;
        Evaluation(const string& target, Dict&& input, ArgFlags flags, Dict&& output) : target(String(target)), input(move(input)), flags(flags), output(move(output)){}
    };
    array<unique<Evaluation>> cache; // Caches argument evaluation
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

    array<string> configure(const ref<string>& allArguments, const string& definition) override;

    /// Gets result from cache or computes if necessary
    shared<Result> getResult(const string& target, const Dict& arguments, const string& scope=""_) override;

    Folder storageFolder; // Should be a RAM (or local disk) filesystem large enough to hold intermediate operations of volume data (TODO: rename to -> temporaryFolder ?)
};
