#include "process.h"
#include "data.h"
#include "time.h"

Process::Process(const ref<byte>& definition, const ref<ref<byte>>& args) {
    // Parses process definition
    for(TextData s(definition); s; s.skip()) {
        if(s.match('#')) { s.until('\n'); continue; }
        Rule rule;
        for(;!s.match('='); s.skip()) rule.outputs << s.word("_-"_);
        s.skip();
        rule.operation = s.word();
        s.whileAny(" \t\r"_);
        for(;!s.match('\n'); s.whileAny(" \t\r"_)) {
            if(s.match('#')) { s.whileNot('\n'); continue; }
            ref<byte> word = s.word("_-"_);
            if(s.match('=')) rule.arguments.insert(word, s.whileNo(" \t\r\n"_));
            else rule.inputs << word;
        }
        rules << move(rule);
    }

    // Parses targets and global arguments
    for(const ref<byte>& argument: args) {
        if(argument.contains('=')) { // Stores generic argument to be parsed in relevant operation
            ref<byte> key=section(argument,'=',0,1), value = section(argument,'=',1,-1);
            /*if(startsWith(value,"{"_) && endsWith(value,"}"_)) { // Replaced by shell
                assert(!arguments.contains(key));
                sweeps.insert(key, apply<Variant>(split(value,','), [](const ref<byte>& o){return o;}));
            } else {*/
            if(sweeps.contains(key) || arguments.contains(key)) {
                if(arguments.contains(key)) sweeps.insert(key, array<Variant>()<<arguments.take(key));
                sweeps.at(key) << value;
            } else {
                arguments.insert(key, (Variant)value);
            }
            continue;
        }
        if(&ruleForOutput(argument)) targets << argument;
    }
    if(!targets) {
        assert(rules && rules.last().outputs, rules);
        targets << rules.last().outputs.last();
    }
}

Rule& Process::ruleForOutput(const ref<byte>& target) { for(Rule& rule: rules) for(const ref<byte>& output: rule.outputs) if(output==target) return rule; return *(Rule*)0; }

Dict Process::relevantArguments(const Rule& rule, const Dict& arguments) {
    Dict relevant;
    for(const ref<byte>& input: rule.inputs) {
        for(auto arg: relevantArguments(ruleForOutput(input), arguments)) {
            if(relevant.contains(arg.key)) assert_(relevant.at(arg.key)==arg.value, "Arguments conflicts", arg.key, relevant.at(arg.key), arg.value);
            else /*if(!defaultArguments.contains(arg.key) || arg.value!=defaultArguments.at(arg.key))*/ relevant.insert(arg.key, arg.value);
        }
    }
    unique<Operation> operation = Interface<Operation>::instance(rule.operation);
    assert_(operation, "Operation", rule.operation, "not found in", Interface<Operation>::factories.keys);
    Dict local = copy(arguments); local << rule.arguments; // not inherited
    for(ref<byte> parameter: split(operation->parameters())) if(local.contains(parameter)) {
        if(relevant.contains(parameter)) assert_(relevant.at(parameter) == local.at(parameter), "Arguments conflicts", parameter, relevant.at(parameter), local.at(parameter));
        else /*if(!defaultArguments.contains(parameter) || local.at(parameter)!=defaultArguments.at(parameter))*/ relevant.insert(parameter, local.at(parameter));
    }
    return relevant;
}

int Process::indexOf(const ref<byte>& target, const Dict& arguments) {
    const Rule& rule = ruleForOutput(target);
    assert_(&rule, "No rule generating '"_+str(target)+"'"_,"in",rules);
    Dict relevantArguments = Process::relevantArguments(ruleForOutput(target), arguments);
    for(uint i: range(results.size)) if(results[i]->name==target && results[i]->relevantArguments==relevantArguments) return i; //FIXME: unserialize map
    return -1;
}
const shared<Result>& Process::find(const ref<byte>& target, const Dict& arguments) { int i = indexOf(target, arguments); return i>=0 ? results[i] : *(shared<Result>*)0; }

/// Returns if computing \a target with \a arguments would give the same result now compared to \a queryTime
bool Process::sameSince(const ref<byte>& target, long queryTime, const Dict& arguments) {
    const shared<Result>& result = find(target, arguments);
    if(&result) {
        if(result->timestamp < queryTime) queryTime = result->timestamp; // Result is still valid if inputs didn't change since it was generated
        else return false; // Result changed since query
    }
    const Rule& rule = ruleForOutput(target);
    for(const ref<byte>& input: rule.inputs) if(!sameSince(input, queryTime, arguments)) return false; // Inputs changed since result (or query if result was discarded) was last generated
    if((long)parse(Interface<Operation>::version(rule.operation)) > queryTime) return false; // Implementation changed since query (FIXME: timestamps might be unsynchronized)
    return true;
}

shared<Result> Process::getResult(const ref<byte>& target, const Dict& arguments) {
    const shared<Result>& result = find(target, arguments);
    if(&result && sameSince(target, result->timestamp, arguments)) { assert(result->data.size); return share(result); }
    error("Anonymous process manager unimplemented"_);
}

PersistentProcess::PersistentProcess(const ref<byte>& definition, const ref<ref<byte>>& arguments) : Process(definition, arguments) {
    for(const ref<byte>& argument: arguments) {
        if(argument.contains('=') || &ruleForOutput(argument)) continue;
        if(!name) name=argument;
        if(existsFolder(argument, currentWorkingDirectory())) name=argument.contains('/')?section(argument,'/',-2,-1):argument;
    }
    assert_(name, arguments);
    storageFolder = Folder(name, baseStorageFolder, true);
    this->arguments.insert("name"_, name);

    // Maps intermediate results from file system
    for(const string& path: storageFolder.list(Files)) {
        if(!&ruleForOutput(name) || !path.contains('{')) { ::remove(path, storageFolder); continue; } // Removes invalid data
        TextData s (path); ref<byte> name = s.whileNot('{'); Dict arguments = parseDict(s); s.skip("["_); ref<byte> metadata = s.until(']');
        File file = File(path, storageFolder, ReadWrite);
        results << shared<ResultFile>(name, file.modifiedTime(), move(arguments), string(metadata), storageFolder, Map(file, Map::Prot(Map::Read|Map::Write)), path);
    }
}

PersistentProcess::~PersistentProcess() {
    results.clear();
    targetResults.clear();
    if(arguments.value("clean"_,"0"_)!="0"_) {
        for(const string& path: storageFolder.list(Files)) ::remove(path, storageFolder); // Cleanups all intermediate results
        remove(storageFolder);
    }
}

shared<Result> PersistentProcess::getResult(const ref<byte>& target, const Dict& arguments) {
    const shared<Result>& result = find(target, arguments);
    if(&result && sameSince(target, result->timestamp, arguments)) return share(result); // Returns a cached result if still valid
    // Otherwise regenerates target using new inputs, arguments and/or implementations
    const Rule& rule = ruleForOutput(target);
    assert_(&rule, target);
    unique<Operation> operation = Interface<Operation>::instance(rule.operation);

    array<shared<Result>> inputs;
    for(const ref<byte>& input: rule.inputs) inputs << getResult(input, arguments);
    for(const shared<Result>& input: inputs) {
        ResultFile& result = *(ResultFile*)input.pointer;
        touchFile(result.fileName, result.folder, false); // Updates last access time for correct LRU cache behavior
    }

    array<shared<Result>> outputs;
    for(uint index: range(rule.outputs.size)) {
        const ref<byte>& output = rule.outputs[index];

        if(&find(output, arguments)) { // Reuses same result file
            shared<ResultFile> result = results.take(indexOf(output, arguments));
            string fileName = move(result->fileName); assert(!result->fileName.size);
            rename(fileName, output, storageFolder);
        }

        Map map;
        int64 outputSize = operation->outputSize(arguments, inputs, index);
        if(outputSize) { // Creates (or resizes) and maps an output result file
            while(outputSize > (existsFile(output, storageFolder) ? File(output, storageFolder).size() : 0) + freeSpace(storageFolder)) {
                long minimum=currentTime()+1; string oldest;
                for(string& path: baseStorageFolder.list(Files|Recursive)) { // Discards oldest unused result (across all process hence the need for ResultFile's inter process reference counter)
                    TextData s (path); s.until('('); uint userCount=s.integer(); if(userCount>1 || !s.match(')')) continue; // Used data or not a process data
                    long timestamp = File(path, baseStorageFolder).accessTime();
                    if(timestamp < minimum) minimum=timestamp, oldest=move(path);
                }
                if(!oldest) { if(outputSize<=1l<<32) error("Not enough space available"); else break; /*Virtual*/ }
                if(section(oldest,'/')==name) {
                    TextData s (section(oldest,'/',1,-1)); ref<byte> name = s.whileNot('{'); Dict arguments = parseDict(s);
                    shared<ResultFile> result = results.take(indexOf(name,arguments));
                    result->fileName.clear(); // Prevents rename
                }
                if(outputSize > File(oldest,baseStorageFolder).size() + freeSpace(storageFolder)) { // Removes if need to recycle more than one file
                    ::remove(oldest, baseStorageFolder);
                    continue;
                }
                ::rename(baseStorageFolder, oldest, storageFolder, output); // Renames last discarded file instead of removing (avoids page zeroing)
                break;
            }

            File file(output, storageFolder, Flags(ReadWrite|Create));
            file.resize( outputSize );
            map = Map(file, Map::Prot(Map::Read|Map::Write), Map::Flags(Map::Shared|(outputSize>1l<<32?0:Map::Populate)));
        }
        outputs << shared<ResultFile>(output, currentTime(), Dict(), string(), storageFolder, move(map), output);
    }
    assert_(outputs);

    Time time;
    Dict relevantArguments = Process::relevantArguments(rule, arguments);
    operation->execute(relevantArguments, outputs, inputs);
    log(left(str(rule),96),left(toASCII(relevantArguments),64),right(str(time),32));

    for(shared<Result>& output : outputs) {
        shared<ResultFile> result = move(output);
        result->relevantArguments = copy(relevantArguments);
        if(result->map) { // Resizes and remaps file read-only (will be remapped Read|Write whenever used as output again)
            assert(result->map.size >= result->data.size);
            result->map.unmap();
            File file(result->fileName, result->folder, ReadWrite);
            file.resize(result->data.size);
            result->map = Map(file);
            result->data = buffer<byte>(result->map);
        } else { // Writes data from heap to file
            writeFile(result->fileName, result->data, result->folder);
        }
        results << move(result);
    }
    return share(find(target, arguments));
}


void Process::execute() {
    for(auto arg: defaultArguments) if(!arguments.contains(arg.key)) arguments.insert(arg.key, arg.value);
    execute(sweeps, arguments);
}
void Process::execute(const map<ref<byte>, array<Variant>>& sweeps, const Dict& arguments) {
    if(sweeps) {
        auto remaining = copy(sweeps);
        auto args = copy(arguments);
        ref<byte> parameter = sweeps.keys.first(); // Removes first parameter and loop over it
        assert(!args.contains(parameter));
        for(Variant& value: remaining.take(parameter)) {
            args.insert(parameter, move(value));
            execute(remaining, args);
            args.remove(parameter);
        }
    } else { // Actually generates targets when sweeps have been explicited
        for(const ref<byte>& target: targets) {
            log(target, relevantArguments(ruleForOutput(target), arguments), results);
            targetResults << getResult(target, arguments);
        }
    }
}
