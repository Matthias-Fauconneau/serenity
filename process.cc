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
            else if(!defaultArguments.contains(arg.key) || arg.value!=defaultArguments.at(arg.key)) relevant.insert(arg.key, arg.value);
        }
    }
    unique<Operation> operation = Interface<Operation>::instance(rule.operation);
    assert_(operation, "Operation", rule.operation, "not found in", Interface<Operation>::factories.keys);
    Dict local = copy(arguments); local << rule.arguments; // not inherited
    for(ref<byte> parameter: split(operation->parameters())) if(local.contains(parameter)) {
        if(relevant.contains(parameter)) assert_(relevant.at(parameter) == local.at(parameter), "Arguments conflicts", parameter, relevant.at(parameter), local.at(parameter));
        else if(!defaultArguments.contains(parameter) || local.at(parameter)!=defaultArguments.at(parameter)) relevant.insert(parameter, local.at(parameter));
    }
    return relevant;
}

bool Process::sameSince(const ref<byte>& target, long queryTime, const Dict& arguments) {
    const Rule& rule = ruleForOutput(target);
    assert_(&rule, "No rule generating '"_+str(target)+"'"_,"in",rules);

    const shared<Result>& result = *results.find(target);
    if(&result) {
        long time = result->timestamp;
        if((long)parse(Interface<Operation>::version(rule.operation)) > queryTime) return false; // Implementation changed since query
        if(time > queryTime || result->arguments != toASCII(relevantArguments(rule,arguments))) return false; // Target changed since query (FIXME: compare maps and not their ASCII representation)
        queryTime = time;
    }
    // Verify inputs didn't change since last evaluation (or query if discarded), in which case target will need to be regenerated
    for(const ref<byte>& input: rule.inputs) if(!sameSince(input, queryTime, arguments)) return false;
    return true;
}

shared<Result> Process::getResult(const ref<byte>& target, const Dict& arguments) {
    const shared<Result>* result = results.find(target);
    if(result && sameSince(target, (*result)->timestamp, arguments)) { assert((*result)->data.size); return share( *result ); }
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
        ref<byte> name = section(path,'.');
        assert_(!results.contains(name));
        if(!&ruleForOutput(name) || !section(path,'.',-3,-2)) { ::remove(path, storageFolder); continue; } // Removes invalid data
        File file = File(path, storageFolder, ReadWrite);
        results << shared<ResultFile>(name, file.modifiedTime(), section(path,'.',-4,-3), section(path,'.',-3,-2), storageFolder, Map(file, Map::Prot(Map::Read|Map::Write)), path);
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
    const shared<Result>* result = results.find(target);
    if(result && sameSince(target, (*result)->timestamp, arguments)) return share( *result );

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

        if(results.contains(output)) { // Reuses same volume
            shared<ResultFile> result = results.take(results.indexOf(output));
            string fileName = move(result->fileName); assert(!result->fileName.size);
            rename(fileName, output, storageFolder);
        }

        Map map;
        int64 outputSize = operation->outputSize(arguments, inputs, index);
        if(outputSize) { // Creates (or resizes) and maps an output result file
            while(outputSize > (existsFile(output, storageFolder) ? File(output, storageFolder).size() : 0) + freeSpace(storageFolder)) {
                long minimum=currentTime()+1; string oldest;
                for(string& path: baseStorageFolder.list(Files|Recursive)) { // Discards oldest unused result (across all process hence the need for ResultFile's inter process reference counter)
                    if(!path.contains('.') || !isInteger(section(path,'.',-2,-1))) continue; // Not a process data
                    uint userCount = toInteger(section(path,'.',-2,-1));
                    if(userCount > 1) continue;
                    long timestamp = File(path, baseStorageFolder).accessTime();
                    if(timestamp < minimum) minimum=timestamp, oldest=move(path);
                }
                if(!oldest) { if(outputSize<=1l<<32) error("Not enough space available"); else break; /*Virtual*/ }
                if(section(oldest,'/')==name) {
                    ref<byte> resultName = section(section(oldest,'/',1,-1),'.');
                    shared<ResultFile> result = results.take(results.indexOf(resultName));
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
        outputs << shared<ResultFile>(output, currentTime(),""_, ""_, storageFolder, move(map), output);
    }
    assert_(outputs);

    Time time;
    operation->execute(arguments<<rule.arguments, outputs, inputs);
    log(left(str(rule),96),left(str(toASCII(relevantArguments(rule, arguments))),64),right(str(time),32));

    for(shared<Result>& output : outputs) {
        shared<ResultFile> result = move(output);
        result->arguments = toASCII(relevantArguments(rule, arguments));
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
    return share( *results.find(target) );
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
