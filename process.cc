#include "process.h"
#include "data.h"
#include "time.h"

Process::Process(const ref<byte>& definition, const ref<ref<byte>>& arguments) {
    // Parses process definition
    for(TextData s(definition); s; s.skip()) {
        if(s.match('#')) { s.until('\n'); continue; }
        Rule rule;
        for(;!s.match('='); s.skip()) rule.outputs << s.word();
        s.skip();
        rule.operation = s.word();
        s.whileAny(" \t\r"_);
        for(;!s.match('\n'); s.whileAny(" \t\r"_)) {
            if(s.match('#')) { s.whileNot('\n'); continue; }
            rule.inputs << s.word();
        }
        rules << move(rule);
    }

    // Parses targets and arguments
    for(const ref<byte>& argument: arguments) {
        if(argument.contains('=')) { this->arguments.insert(section(argument,'=',0,1), (Variant)section(argument,'=',1,-1)); continue; } // Stores generic argument to be parsed in relevant operation
        if(ruleForOutput(argument) && !target) target=argument;
    }
    if(!target) {
        assert(rules && rules.last().outputs, rules);
        target=rules.last().outputs.last();
    }
}

PersistentProcess::PersistentProcess(const ref<byte>& definition, const ref<ref<byte>>& arguments) : Process(definition, arguments) {
    for(const ref<byte>& argument: arguments) {
        if(argument.contains('=') || ruleForOutput(argument)) continue;
        if(!name) name=argument;
        if(existsFolder(argument)) name=argument.contains('/')?section(argument,'/',-2,-1):argument;
    }
    assert(name);
    storageFolder = Folder(name, baseStorageFolder, true);
    this->arguments.insert("name"_, name);

    // Maps intermediate results from file system
    for(const string& path: storageFolder.list(Files)) {
        ref<byte> name = section(path,'.');
        assert_(!results.contains(name));
        if(!ruleForOutput(name) || !section(path,'.',1,2)) { ::remove(path, storageFolder); continue; } // Removes invalid data
        File file = File(path, storageFolder, ReadWrite);
        assert(file.size());
        results << shared<ResultFile>(name, file.modifiedTime(), section(path,'.',-3,-2), storageFolder, Map(file, Map::Prot(Map::Read|Map::Write)), path);
    }
}

PersistentProcess::~PersistentProcess() {
    results.clear();
    if(arguments.value("clean"_,"0"_)!="0"_) {
        for(const string& path: storageFolder.list(Files)) ::remove(path, storageFolder); // Cleanups intermediate data
        remove(storageFolder);
    }
}

const Rule* Process::ruleForOutput(const ref<byte>& target) {
    for(const Rule& rule: rules) for(const ref<byte>& output: rule.outputs) if(output==target) return &rule; return 0;
}

bool Process::sameSince(const ref<byte>& target, long queryTime) {
    const Rule& rule = *ruleForOutput(target);
    assert_(&rule, target);

    const shared<Result>* result = results.find(target);
    if(result) {
        long time = (*result)->timestamp;
        if(time > queryTime) return false; // Target changed since query
        queryTime = time;
    }
    // Verify inputs didnt change since last evaluation (or query if discarded), in which case target will need to be regenerated
    for(const ref<byte>& input: rule.inputs) if(!sameSince(input, queryTime)) return false;
    return true;
}

shared<Result> Process::getVolume(const ref<byte>& target) {
    const shared<Result>* result = results.find(target);
    if(result && sameSince(target, (*result)->timestamp)) { assert((*result)->data.size); return share( *result ); }
    error("Anonymous process manager unimplemented"_);
}

shared<Result> PersistentProcess::getVolume(const ref<byte>& target) {
    const shared<Result>* result = results.find(target);
    if(result && sameSince(target, (*result)->timestamp)) { assert((*result)->data.size); return share( *result ); }

    const Rule& rule = *ruleForOutput(target);
    assert_(&rule, target);
    unique<Operation> operation = Interface<Operation>::instance(rule.operation);

    array<shared<Result>> inputs;
    for(const ref<byte>& input: rule.inputs) inputs << getVolume( input );

    array<shared<Result>> outputs;
    for(uint index: range(rule.outputs.size)) {
        const ref<byte>& output = rule.outputs[index];

        if(results.contains(output)) { // Reuses same volume
            shared<ResultFile> result = results.take(results.indexOf(output));
            rename(move(result->oldName), output, storageFolder);
        }
        // Creates (or resizes) and maps an output result file
        int64 outputSize = operation->outputSize(arguments, inputs, index);
        while(outputSize > (existsFile(output, storageFolder) ? File(output, storageFolder).size() : 0) + freeSpace(storageFolder)) {
            long minimum=currentTime()+1; string oldest;
            for(string& path: baseStorageFolder.list(Files|Recursive)) { // Discards oldest unused result (across all process hence the need for ResultFile's inter process reference counter)
                uint userCount = toInteger(section(path,'.',-2,-1));
                if(userCount > 1) continue;
                long timestamp = File(path, baseStorageFolder).accessTime();
                if(timestamp < minimum) minimum=timestamp, oldest=move(path);
            }
            if(!oldest) error("Not enough space available");
            if(section(oldest,'/')==name) results.remove( section(section(oldest,'/',1,-1),'.') );
            if(outputSize > File(oldest,baseStorageFolder).size() + freeSpace(output, storageFolder)) {
                ::remove(oldest, baseStorageFolder); // Removes if need to recycle more than one file
                continue;
            }
            // Renames last discarded file instead of removing (avoids page zeroing)
            ::rename(baseStorageFolder, oldest, storageFolder, output);
            break;
        }

        File file(output, storageFolder, Flags(ReadWrite|Create));
        file.resize( outputSize );
        outputs << shared<ResultFile>(output, currentTime(), ""_, storageFolder, Map(file, Map::Prot(Map::Read|Map::Write)), output);
    }
    assert_(outputs);

    Time time;
    operation->execute(arguments, outputs, inputs);
    log(rule, time);

    results << move(outputs);
    return share( *results.find(target) );
}
