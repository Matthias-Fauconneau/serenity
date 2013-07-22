#pragma once
#include "operation.h"
#include "file.h"

/// Defines a production rule to evaluate outputs using an operation and any associated arguments
struct Rule {
    struct { string op, parameter, value; } condition;
    string operation;
    array<string> inputs;
    array<string> outputs;
    map<String, Variant> arguments;
    array<string> processParameters;
    array<string> parameters() const;
};
template<> inline String str(const Rule& rule) { return str(rule.outputs,"=",rule.operation,rule.inputs); }

/// Manages a process defined a direct acyclic graph of production rules
struct Process {

    /// Configures process using given arguments and definition (which can depends on the arguments)
    virtual array<string> configure(const ref<string>& allArguments, const string& definition);

    /// Parses special arguments
    virtual void parseSpecialArguments(const ref<string>& arguments) { assert_(!arguments); }

    /// Returns the Rule to evaluate in order to produce \a target
    Rule& ruleForOutput(const string& target, const Dict& arguments);

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

    /// Gets result from cache or computes if necessary
    virtual shared<Result> getResult(const string& target, const Dict& arguments) abstract;

    /// Computes result of an operation
    virtual void compute(const string& operation, const ref<shared<Result>>& inputs, const ref<string>& outputNames, const Dict& arguments, const Dict& relevantArguments, const Dict& localArguments) abstract;

    array<string> specialParameters; // Valid parameters accepted for derived class special behavior
    Dict arguments; // User-specified arguments
    Dict specialArguments; // User-specified special arguments
    array<Rule> rules; // Production rules
    array<string> resultNames; // Valid result names defined by process
    struct Evaluation { String target; Dict input; Dict output; Evaluation(String&& target, Dict&& input, Dict&& output) : target(move(target)), input(move(input)), output(move(output)){}};
    array<unique<Evaluation>> cache; // Caches argument evaluation
    array<shared<Result>> results; // Generated intermediate (and target) data
};

/// High level operation with direct access to query new results from process
/// \note as inputs are unknown, results are not regenerated on input changes
struct Tool {
    /// Returns which parameters affects this operation output
    virtual string parameters() const { return ""_; }
    /// Executes the tool computing data results using process
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<Result*>& inputs, Process& results) abstract;
    /// Virtual destructor
    virtual ~Tool() {}
};

/// Mirrors results on a filesystem
struct ResultFile : Result {
    ResultFile(const string& name, long timestamp, Dict&& arguments, String&& metadata, String&& data, const string& id, const string& folder)
        : Result(name,timestamp,move(arguments),move(metadata), move(data)), id(String(id)), folder(String(folder)), fileID(indirectID ? indirectID++ : 0) {}
    ResultFile(const string& name, long timestamp, Dict&& arguments, String&& metadata, Map&& map, const string& id, const string& folder)
        : Result(name,timestamp,move(arguments),move(metadata), buffer<byte>(map)), id(String(id)), folder(String(folder)), fileID(indirectID ? indirectID++ : 0) { if(map) maps<<move(map); }
    void rename() {
        if(!id || !name) return;
        rename( name+"{"_+toASCII(relevantArguments)+"}"_+(userCount?str(userCount):String())+"."_+metadata );
    }
    void rename(const string& newID) {
        if(id!=newID) {
            if(indirectID) writeFile(str(fileID)+".meta"_, newID, Folder(folder));
            else ::rename(id, newID, Folder(folder));
            id=String(newID);
        }
    }
    void addUser() override { ++userCount; rename(); }
    uint removeUser() override { --userCount; rename(); return userCount; }
    String dataFile() { return indirectID ? str(fileID)+".data"_ : copy(id); }

    array<Map> maps;
    String id;
    String folder; // a Folder file descriptor would use up maximum file descriptor count (ulimit -n)
    static uint indirectID;
    uint fileID;
};

/// Mirrors a process intermediate data on the filesystem for persistence and operations using multiple processes
struct PersistentProcess : virtual Process {
     PersistentProcess(const ref<byte>& name) : storageFolder(name,Folder("dev/shm"_),true) { specialParameters += "storageFolder"_; specialParameters += "indirect"_; }
    ~PersistentProcess();

     /// Maps intermediate results from file system
    array<string> configure(const ref<string>& allArguments, const string& definition) override;

    /// Gets result from cache or computes if necessary
    shared<Result> getResult(const string& target, const Dict& arguments) override;

    /// Computes result of an operation
    void compute(const string& operation, const ref<shared<Result>>& inputs, const ref<string>& outputNames, const Dict& arguments, const Dict& relevantArguments, const Dict& localArguments) override;

    Folder storageFolder; // Should be a RAM (or local disk) filesystem large enough to hold intermediate operations of volume data (TODO: rename to -> temporaryFolder ?)
};
