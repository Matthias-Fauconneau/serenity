#include "process.h"
#include "data.h"
#include "time.h"

Process::Process(const ref<byte>& definition, const ref<ref<byte>>& args) {
    // Parses process definition
    array<ref<byte>> results;
    for(TextData s(definition); s; s.skip()) {
        if(s.match('#')) { s.until('\n'); continue; }
        Rule rule;
        for(;!s.match('='); s.skip()) {
            ref<byte> output = s.word("_-"_);
            assert_(!results.contains(output), "Multiple definitions for", output);
            results << output;
            rule.outputs << output;
        }
        s.skip();
        rule.operation = s.word();
        assert_(rule.operation);
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
                arguments.insert(key, Variant(value));
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
            else /*if(!defaultArguments.contains(arg.key) || arg.value!=defaultArguments.at(arg.key))*/ relevant.insert(move(arg.key), move(arg.value));
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
    for(uint i: range(results.size)) if(results[i]->name==target && results[i]->relevantArguments==relevantArguments) return i;
    return -1;
}
const shared<Result>& Process::find(const ref<byte>& target, const Dict& arguments) { int i = indexOf(target, arguments); return i>=0 ? results[i] : *(shared<Result>*)0; }

/// Returns if computing \a target with \a arguments would give the same result now compared to \a queryTime
bool Process::sameSince(const ref<byte>& target, int64 queryTime, const Dict& arguments) {
    const shared<Result>& result = find(target, arguments);
    if(&result) {
        if(result->timestamp <= queryTime) queryTime = result->timestamp; // Result is still valid if inputs didn't change since it was generated
        else return false; // Result changed since query
    }
    const Rule& rule = ruleForOutput(target);
    for(const ref<byte>& input: rule.inputs) if(!sameSince(input, queryTime, arguments)) return false; // Inputs changed since result (or query if result was discarded) was last generated
    if(parse(Interface<Operation>::version(rule.operation))*1000000000l > queryTime) return false; // Implementation changed since query (FIXME: timestamps might be unsynchronized)
    //else log("same",parse(Interface<Operation>::version(rule.operation), Date(queryTime), (long)parse(Interface<Operation>::version(rule.operation)), queryTime);
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
    for(const string& path: storageFolder.list(Files|Folders)) {
        TextData s (path); ref<byte> name = s.whileNot('{');
        if(path==name || !&ruleForOutput(name)) { ::remove(path, storageFolder); continue; } // Removes invalid data
        Dict arguments = parseDict(s); s.until("."_); ref<byte> metadata = s.untilEnd();
        if(!existsFolder(path, storageFolder)) {
            File file = File(path, storageFolder, ReadWrite);
            if(file.size()<pageSize) { // Small file (<4K)
                results << shared<ResultFile>(name, file.modifiedTime(), move(arguments), string(metadata), file.read(file.size()), path, storageFolder);
            } else { // Memory-mapped file
                results << shared<ResultFile>(name, file.modifiedTime(), move(arguments), string(metadata), Map(file, Map::Prot(Map::Read|Map::Write)), path, storageFolder);
            }
        } else { // Folder
            Folder folder (path, storageFolder);
            shared<ResultFile> result(name, folder.modifiedTime(), move(arguments), string(metadata), string(), path, storageFolder);
            for(const string& path: folder.list(Files)) {
                File file = File(path, folder, ReadWrite);
                if(file.size()<pageSize) { // Small file (<4K)
                    result->elements << file.read(file.size());
                } else { // Memory-mapped file
                    result->maps << Map(file, Map::Prot(Map::Read|Map::Write));
                    result->elements << buffer<byte>(result->maps.last());
                }
            }
            results << move(result);
        }
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
        int64 outputSize = operation->outputSize(arguments, cast<Result*>(inputs), index);
        if(outputSize) { // Creates (or resizes) and maps an output result file
            while(outputSize > (existsFile(output, storageFolder) ? File(output, storageFolder).size() : 0) + freeSpace(storageFolder)) {
                long minimum=currentTime()+1; string oldest;
                for(string& path: baseStorageFolder.list(Files|Recursive)) { // Discards oldest unused result (across all process hence the need for ResultFile's inter process reference counter)
                    TextData s (path); s.until('}'); uint userCount=s.integer(); if(userCount>1 || !s.match('.')) continue; // Used data or not a process data
                    long timestamp = File(path, baseStorageFolder).accessTime();
                    if(timestamp < minimum) minimum=timestamp, oldest=move(path);
                }
                if(!oldest) { if(outputSize<=1l<<32) error("Not enough space available"); else break; /*Virtual*/ }
                if(section(oldest,'/')==name) {
                    TextData s (section(oldest,'/',1,-1)); ref<byte> name = s.whileNot('{'); Dict arguments = parseDict(s);
                    int i = indexOf(name,arguments);
                    assert_(i>=0, name, arguments, results);
                    shared<ResultFile> result = results.take(i);
                    result->fileName.clear(); // Prevents rename
                }
                if(!existsFile(oldest, baseStorageFolder) || outputSize > File(oldest,baseStorageFolder).size() + freeSpace(storageFolder)) { // Removes if not a file or need to recycle more than one file
                    if(existsFile(oldest, baseStorageFolder)) ::remove(oldest, baseStorageFolder);
                    else { // Array output (folder)
                        Folder folder(oldest, baseStorageFolder);
                        for(const string& path: folder.list(Files)) ::remove(path, folder);
                        remove(folder);
                    }
                    continue;
                }
                ::rename(baseStorageFolder, oldest, storageFolder, output); // Renames last discarded file instead of removing (avoids page zeroing)
                break;
            }

            File file(output, storageFolder, Flags(ReadWrite|Create));
            file.resize(outputSize);
            if(outputSize>=pageSize) map = Map(file, Map::Prot(Map::Read|Map::Write), Map::Flags(Map::Shared|(outputSize>1l<<32?0:Map::Populate)));
        }
        outputs << shared<ResultFile>(output, currentTime(), Dict(), string(), move(map), output, storageFolder);
    }
    assert_(outputs);

    Time time;
    Dict relevantArguments = Process::relevantArguments(rule, arguments);
    operation->execute(relevantArguments, cast<Result*>(outputs), cast<Result*>(inputs));
    log(rule, time);

    for(shared<Result>& output : outputs) {
        shared<ResultFile> result = move(output);
        result->timestamp = realTime();
        result->relevantArguments = copy(relevantArguments);
        if(result->elements) { // Copies each elements data from anonymous memory to numbered files in a folder
            assert_(!result->maps);
            Folder folder(result->fileName, result->folder, true);
            for(uint i: range(result->elements.size)) writeFile(dec(i,4)+"."_+result->metadata, result->elements[i], folder);
            touchFile(result->fileName, result->folder, true);
        } else { // Synchronizes file mappings with results
            uint64 mappedSize = 0;
            if(result->maps) {
                assert_(result->maps.size == 1);
                mappedSize = result->maps[0].size;
                assert_(mappedSize);
                if(mappedSize<pageSize) result->data = copy(result->data); // Copies to anonymous memory before unmapping
                result->maps.clear();
            }
            File file = 0;
            if(result->data.size <= mappedSize) { // Truncates file to result size
                file = File(result->fileName, result->folder, ReadWrite);
                file.resize(result->data.size);
            } else { // Copies data from anonymous memory to file
                file = File(result->fileName, result->folder, Flags(ReadWrite|Truncate|Create));
                file.write(result->data);
            }
            if(mappedSize>=pageSize) { // Remaps file read-only (will be remapped Read|Write whenever used as output again)
                result->maps << Map(file);
                result->data = buffer<byte>(result->maps.last());
            }
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
        map<ref<byte>, array<Variant>> remaining = copy(sweeps);
        Dict args = copy(arguments);
        ref<byte> parameter = sweeps.keys.first(); // Removes first parameter and loop over it
        assert(!args.contains(parameter));
        for(Variant& value: remaining.take(parameter)) {
            args.insert(parameter, move(value));
            execute(remaining, args);
            args.remove(parameter);
        }
    } else { // Actually generates targets when sweeps have been explicited
        for(const ref<byte>& target: targets) {
            log(target, relevantArguments(ruleForOutput(target), arguments));
            targetResults << getResult(target, arguments);
        }
    }
}
