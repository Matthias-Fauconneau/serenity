#include "process.h"
#include "data.h"
#include "time.h"
#include "math.h"

/// Relevant operation parameters
array<string> Rule::parameters() const {
    array<string> parameters = copy(processParameters);
    if(operation && Interface<Operation>::factories.contains(this->operation)) {
        unique<Operation> operation = Interface<Operation>::instance(this->operation);
        assert_(operation, "Operation", this->operation, "not found in", Interface<Operation>::factories.keys);
        parameters += split(operation->parameters());
    }
    return parameters;
}

array<string> Process::parameters() {
    array<string> parameters;
    for(auto factory: Interface<Operation>::factories.values) parameters += split(factory->constructNewInstance()->parameters());
    return parameters;
}

array<string> Process::configure(const ref<string>& allArguments, const string& definition) {
    array<string> targets;
    Dict defaultArguments; // Process-specified default arguments

    array<string> parameters = this->parameters();

    for(TextData s(definition); s;) { //FIXME: use parser generator
        s.skip();
        if(s.match('#')) { s.until('\n'); continue; }
        array<string> processParameters;
        Rule rule;
        if(s.match("if"_)) {
            s.whileAny(" \t"_);
            string op;
            string value;
            if(s.match('!')) op="=="_, value="0"_;
            string parameter = s.word("_-."_);
            parameters += parameter;
            processParameters += parameter;
            s.whileAny(" \t"_);
            if(!op) op = s.whileAny("!="_);
            if(!op) op = "!="_, value = "0"_;
            if(!value) { s.whileAny(" \t"_); s.skip("'"_); value=s.until('\''); }
            s.skip(":"_);
            assert_(parameter && value);
            rule.condition={op, parameter, value};
            s.whileAny(" \t"_);
        }
        array<string> outputs;
        for(;!s.match('='); s.whileAny(" \t"_)) {
            string output = s.match('\'') ? s.until('\'') : s.whileNo(" \t=");
            assert_(output, s.until('\n'));
            outputs << output;
        }
        assert_(outputs, s.until('\n'));
        s.whileAny(" \t"_);
        if(outputs.size==1 && s.peek()=='\'') { // Default argument
            assert_(!rule.condition.op, "Conditionnal default argument would induce cyclic dependency between process definition and user arguments"_);
            string key = outputs[0];
            parameters += key; // May not be defined yet
            if(!s.match('\'')) error("Unquoted literal", key, s.whileNo(" \t\n"_));
            string value = s.until('\''); // Literal
            assert_(!defaultArguments.contains(key),"Multiple default argument definitions for",key);
            defaultArguments.insert(String(key), String(value));
        } else {
            string word = s.word("_-"_);
            assert_(word, "Expected operator or input for", outputs);
            if(!Interface<Operation>::factories.contains(word) && !Interface<Tool>::factories.contains(word)) rule.inputs << word; // Forwarding rule
            else rule.operation = word; // Generating rule
            s.whileAny(" \t"_);
            for(;!s.match('\n'); s.whileAny(" \t"_)) {
                if(s.match('#')) { s.whileNot('\n'); continue; }
                string key = s.word("_-."_); s.whileAny(" \t"_);
                assert_(key, s.until('\n'), word);
                if(s.match('=')) { // Local argument
                    s.whileAny(" \t"_);
                    //s.skip("'"_);
                    rule.arguments.insert(String(key), String(/*s.until('\'')*/ s.whileNo(" \t\n")));
                }
                else if(resultNames.contains(key)) rule.inputs << key; // Result input
                else if(rule.operation) rule.inputs << key; // Argument value
            }
            resultNames += outputs; //for(string output: outputs) { assert_(!resultNames.contains(output), "Multiple result definitions for", output); resultNames << output; }
            rule.outputs = move(outputs);
            rule.processParameters = move(processParameters);
            rules << move(rule);
        }
    }

    array<string> specialArguments;
    for(const string& argument: allArguments) { // Parses process arguments
        TextData s (argument); string key = s.word("-_."_);
        string scope, parameter = key;
        if(key.contains('.')) scope=section(key, '.', 0, 1), parameter=section(key, '.', 1, 2);
        if(s.match('=')) { // Explicit argument
            assert_(parameters.contains(parameter) || specialParameters.contains(parameter),"Invalid parameter", parameter);
            string value = s.untilEnd();
            assert_(value);
            assert_(!arguments.contains(key), key);
            arguments.insert(String(key), String(value));
        }
        else if(resultNames.contains(argument)) targets << argument;
        else if(parameters.contains(parameter)) arguments.insert(String(argument), String());
        else if(specialParameters.contains(argument)) this->specialArguments.insert(String(argument), String());
        else specialArguments << argument;
    }
    for(auto arg: defaultArguments) if(!arguments.contains(arg.key)) arguments.insert(copy(arg.key), copy(arg.value));
    this->parseSpecialArguments(specialArguments);
    return targets;
}

Rule& Process::ruleForOutput(const string& target, const Dict& arguments) {
    array<Rule*> match;
    for(Rule& rule: rules) for(const string& output: rule.outputs) if(output==target) {
        bool enable = false;
        string op=rule.condition.op, parameter=rule.condition.parameter, value=rule.condition.value;
        string argument = arguments.value(parameter,"0"_); if(!argument) argument="1"_;
        if(!op) enable = true;
        else if(op=="=="_) enable = (argument == value);
        else if(op=="!="_) enable = (argument != value);
        else error("Unknown operator", "'"_+op+"'"_);
        if(enable) match << &rule;
    }
    assert_(match.size<=1);
    return match ? *match.first() : *(Rule*)0;
}

/// Returns recursively relevant global and scoped arguments
const Dict& Process::relevantArguments(const string& target, const Dict& arguments) {
    for(const Evaluation& e: cache) if(e.target==target && e.input==arguments) return e.output;
    const Rule& rule = ruleForOutput(target, arguments);
    Dict args;
    if(!&rule && arguments.contains(target)) { // Conversion from argument to result
        args.insert(String(target), copy(arguments.at(target)));
        assert_(args);
        cache << unique<Evaluation>(String(target), copy(arguments), move(args));
        return cache.last()->output;
    }
    assert_(&rule, "No rule generating '"_+target+"'"_, arguments);

    // Recursively evaluates relevant arguments to invalid cache
    for(const string& input: rule.inputs) {
        for(auto arg: relevantArguments(input, arguments)) {
            assert_(args.value(arg.key,arg.value)==arg.value, target, arg.key, args.at(arg.key), arg.value);
            if(!args.contains(arg.key)) args.insert(copy(arg.key), copy(arg.value));
        }
    }
    array<string> parameters = rule.parameters();
    for(auto arg: arguments) { // Appends relevant global arguments
        assert_(args.value(arg.key,arg.value)==arg.value);
        if(parameters.contains(arg.key)) if(!args.contains(arg.key)) args.insert(copy(arg.key), copy(arg.value));
    }
    for(auto arg: arguments) { // Appends matching scoped arguments
        string scope, parameter = arg.key;
        if(arg.key.contains('.')) scope=section(arg.key, '.', 0, 1), parameter=section(arg.key, '.', 1, 2);
        if(!scope) continue;
        for(const string& output: rule.outputs) if(output==scope) goto match;
        /*else*/ continue;
match:
        assert_(parameters.contains(parameter), "Irrelevant parameter", scope+"."_+parameter, "for"_, rule);
        //if(args.contains(parameter)) args.remove(parameter);
        if(args.contains(arg.key)) assert_(args.at(arg.key)==arg.value);
        else args.insert(copy(arg.key), copy(arg.value));
    }
    cache << unique<Evaluation>(String(target), copy(arguments), move(args));
    return cache.last()->output;
}

/// Returns recursively relevant global, local and explicits scoped arguments
Dict Process::localArguments(const string& target, const Dict& arguments) {
    Dict args = copy(relevantArguments(target, arguments));
    const Rule& rule = ruleForOutput(target, arguments);
    array<string> parameters = rule.parameters();
    for(auto arg: rule.arguments) { // Appends local arguments
        assert_(parameters.contains(arg.key),target,arg.key);
        if(args.contains(arg.key)) args.remove(arg.key); // Local arguments overrides scope arguments
        TextData s(arg.value); string scope=s.until('.');
        if(&ruleForOutput(scope, arguments)) { if(arguments.contains(arg.value)) args.insert(copy(arg.key), copy(arguments.at(arg.value))); } // Argument value
        else args.insert(copy(arg.key), copy(arg.value)); // Argument literal
    }
    for(auto arg: arguments) { // Appends matching scoped arguments
        string scope, parameter = arg.key;
        if(arg.key.contains('.')) scope=section(arg.key, '.', 0, 1), parameter=section(arg.key, '.', 1, 2);
        if(!scope) continue;
        for(const string& output: rule.outputs) if(output==scope) goto match;
        /*else*/ continue;
match:
        assert_(parameters.contains(parameter), "Irrelevant parameter", scope+"."_+parameter, "for"_, rule);
        if(args.contains(parameter)) args.remove(parameter);
        if(args.contains(arg.key)) args.remove(arg.key); // Cleanup scoped parameter (local only)
        args.insert(String(parameter), copy(arg.value));
    }
    return args;
}

int Process::indexOf(const string& target, const Dict& arguments) {
    const Dict& relevantArguments = Process::localArguments(target, arguments);
    for(uint i: range(results.size)) if(results[i]->name==target && results[i]->relevantArguments==relevantArguments) return i;
    return -1;
}
const shared<Result>& Process::find(const string& target, const Dict& arguments) { int i = indexOf(target, arguments); return i>=0 ? results[i] : *(shared<Result>*)0; }

/// Returns if computing \a target with \a arguments would give the same result now compared to \a queryTime
bool Process::sameSince(const string& target, int64 queryTime, const Dict& arguments) {
    const Rule& rule = ruleForOutput(target, arguments);
    if(!&rule && arguments.contains(target)) return true; // Conversion from argument to result
    assert_(&rule, target);
    const shared<Result>& result = find(target, arguments);
    if(&result) {
        if(result->timestamp <= queryTime) queryTime = result->timestamp; // Result is still valid if inputs didn't change since it was generated
        else return false; // Result changed since query
    }
    for(const string& input: rule.inputs) { // Inputs changed since result (or query if result was discarded) was last generated
        if(!sameSince(input, queryTime, arguments)) return false;
    }
    if(rule.operation) { // Implementation changed since query
        if(Interface<Operation>::factories.contains(rule.operation) && parse(Interface<Operation>::version(rule.operation))*1000000000l > queryTime) return false;
        if(Interface<Tool>::factories.contains(rule.operation) && parse(Interface<Tool>::version(rule.operation))*1000000000l > queryTime) return false;
    }
    return true;
}

array<string> PersistentProcess::configure(const ref<string>& allArguments, const string& definition) {
    assert_(!results);
    array<string> targets = Process::configure(allArguments, definition);
    if(arguments.contains("storageFolder"_)) storageFolder = Folder(arguments.at("storageFolder"_),currentWorkingDirectory());
    // Maps intermediate results from file system
    for(const String& path: storageFolder.list(Files|Folders)) {
        TextData s (path); string name = s.whileNot('{');
        if(path==name || !&ruleForOutput(name, arguments)) { // Removes invalid data
            if(existsFolder(path,storageFolder)) {
                for(const string& file: Folder(path,storageFolder).list(Files)) ::remove(file,Folder(path,storageFolder));
                ::removeFolder(path, storageFolder);
            } else ::remove(path, storageFolder);
            continue;
        }
        Dict arguments = parseDict(s); s.until("."_); string metadata = s.untilEnd();
        if(!existsFolder(path, storageFolder)) {
            File file = File(path, storageFolder, ReadWrite);
            if(file.size()<pageSize) { // Small file (<4K)
                results << shared<ResultFile>(name, file.modifiedTime(), move(arguments), String(metadata), file.read(file.size()), path, storageFolder.name());
            } else { // Memory-mapped file
                results << shared<ResultFile>(name, file.modifiedTime(), move(arguments), String(metadata), Map(file, Map::Prot(Map::Read|Map::Write)), path, storageFolder.name());
            }
        } else { // Folder
            Folder folder (path, storageFolder);
            shared<ResultFile> result(name, folder.modifiedTime(), move(arguments), String(metadata), String(), path, storageFolder.name());
            for(const String& path: folder.list(Files|Sorted)) {
                string key = section(path,'.',0,1), metadata=section(path,'.',1,-1);
                assert_(metadata == result->metadata);
                File file = File(path, folder, ReadWrite);
                if(file.size()<pageSize) { // Small file (<4K)
                    result->elements.insert(String(key), file.read(file.size()));
                } else { // Memory-mapped file
                    result->maps << Map(file, Map::Prot(Map::Read|Map::Write));
                    result->elements.insert(String(key), buffer<byte>(result->maps.last()));
                }
            }
            results << move(result);
        }
    }
    return move(targets);
}

PersistentProcess::~PersistentProcess() {
    results.clear();
    if(arguments.value("clean"_,"0"_)!="0"_) {
        for(const String& path: storageFolder.list(Files)) ::remove(path, storageFolder); // Cleanups all intermediate results
        remove(storageFolder);
    }
}

shared<Result> PersistentProcess::getResult(const string& target, const Dict& arguments) {
    const Rule& rule = ruleForOutput(target, arguments);
    if(!&rule && arguments.contains(target)) // Conversion from argument to result
        return shared<ResultFile>(target, 0, Dict(), String("argument"_), copy(arguments.at(target)), ""_, ""_);
    assert_(&rule, "Unknown rule", target);

    // Simple forwarding rule
    if(!rule.operation) {
        assert_(!rule.arguments && rule.inputs.size == 1 && rule.outputs.size==1, "FIXME: Only single inputs can be forwarded");
        return getResult(rule.inputs.first(), arguments);
    }

    const shared<Result>& result = find(target, arguments);
    if(&result && sameSince(target, result->timestamp, arguments)) return share(result); // Returns a cached result if still valid
    // Otherwise regenerates target using new inputs, arguments and/or implementations

    array<shared<Result>> inputs;
    for(const string& input: rule.inputs) inputs << getResult(input, arguments);
    for(const shared<Result>& input: inputs) {
        ResultFile& result = *(ResultFile*)input.pointer;
        if(result.fileName) touchFile(result.fileName, result.folder, false); // Updates last access time for correct LRU cache behavior
    }

    unique<Operation> operation = Interface<Operation>::factories.contains(rule.operation) ? Interface<Operation>::instance(rule.operation) : nullptr;
    Dict relevantArguments = this->localArguments(target, arguments);
    Dict localArguments; array<string> parameters = rule.parameters();
    for(auto arg: relevantArguments) if(parameters.contains(arg.key)) localArguments.insert(copy(arg.key), copy(arg.value)); // Filters locally relevant arguments

    array<shared<Result>> outputs;
    for(uint index: range(rule.outputs.size)) {
        const string& output = rule.outputs[index];

        if(&find(output, arguments)) { // Reuses same result file
            shared<ResultFile> result = results.take(indexOf(output, arguments));
            String fileName = move(result->fileName); assert_(!result->fileName);
            rename(fileName, output, storageFolder);
        }

        Map map;
        int64 outputSize = operation ? operation->outputSize(localArguments, cast<Result*>(inputs), index) : 0;
        if(outputSize) { // Creates (or resizes) and maps an output result file
            while(outputSize >= (existsFile(output, storageFolder) ? File(output, storageFolder).size() : 0) + freeSpace(storageFolder)) {
                long minimum=realTime(); String oldest;
                for(String& path: storageFolder.list(Files)) { // Discards oldest unused result (across all process hence the need for ResultFile's inter process reference counter)
                    TextData s (path); s.until('}'); int userCount=s.mayInteger(); if(userCount>1 || !s.match('.')) continue; // Used data or not a process data
                    if(File(path, storageFolder).size() < 64*1024) continue; // Small files won't release much capacity
                    long timestamp = File(path, storageFolder).accessTime();
                    if(timestamp < minimum) minimum=timestamp, oldest=move(path);
                }
                if(!oldest) { if(outputSize<=1l<<32) error("Not enough space available"); else break; /*Virtual*/ }
                TextData s (oldest); string name = s.whileNot('{'); Dict relevantArguments = parseDict(s);
                for(uint i: range(results.size)) if(results[i]->name==name && results[i]->relevantArguments==relevantArguments) {
                    ((shared<ResultFile>)results.take(i))->fileName.clear(); // Prevents rename
                    break;
                }
                if(!existsFile(oldest, storageFolder) || outputSize > File(oldest,storageFolder).size() + freeSpace(storageFolder)) { // Removes if not a file or need to recycle more than one file
                    if(existsFile(oldest, storageFolder)) ::remove(oldest, storageFolder);
                    else { // Array output (folder)
                        Folder folder(oldest, storageFolder);
                        for(const String& path: folder.list(Files)) ::remove(path, folder);
                        remove(folder);
                    }
                    continue;
                }
                ::rename(storageFolder, oldest, storageFolder, output); // Renames last discarded file instead of removing (avoids page zeroing)
                break;
            }

            File file(output, storageFolder, Flags(ReadWrite|Create));
            file.resize(outputSize);
            if(outputSize>=pageSize) map = Map(file, Map::Prot(Map::Read|Map::Write), Map::Flags(Map::Shared|(outputSize>1l<<32?0:Map::Populate)));
        }
        outputs << shared<ResultFile>(output, currentTime(), Dict(), String(), move(map), output, storageFolder.name());
    }
    assert_(outputs);

    Time time;
    if(operation) operation->execute(localArguments, cast<Result*>(outputs), cast<Result*>(inputs));
    else Interface<Tool>::instance(rule.operation)->execute(arguments, cast<Result*>(outputs), cast<Result*>(inputs), *this);
    if((uint64)time>200) log(rule, localArguments ? str(localArguments) : ""_, time);

    for(shared<Result>& output : outputs) {
        shared<ResultFile> result = move(output);
        result->timestamp = realTime();
        result->relevantArguments = copy(relevantArguments);
        if(result->elements) { // Copies each elements data from anonymous memory to files in a folder
            assert_(!result->maps && !result->data);
            assert_(result->elements.size() > 1);
            Folder folder(result->fileName, result->folder, true);
            for(string file: folder.list(Files)) remove(file,folder);
            for(const_pair<String,buffer<byte>> element: (const map<String,buffer<byte>>&)result->elements) writeFile(element.key+"."_+result->metadata, element.value, folder);
            touchFile(result->fileName, result->folder, true);
        } else { // Synchronizes file mappings with results
            size_t mappedSize = 0;
            if(result->maps) {
                assert_(result->maps.size == 1);
                mappedSize = result->maps[0].size;
                assert_(mappedSize);
                if(mappedSize<pageSize) result->data = copy(result->data); // Copies to anonymous memory before unmapping
                result->maps.clear();
            }
            File file = 0;
            if(mappedSize && result->data.size <= mappedSize) { // Truncates file to result size
                file = File(result->fileName, result->folder, ReadWrite);
                if(result->data.size < mappedSize) file.resize(result->data.size);
                //else only open file to map read-only
            } else { // Copies data from anonymous memory to file
                file = File(result->fileName, result->folder, Flags(ReadWrite|Truncate|Create));
                assert(result->data);
                file.write(result->data);
            }
            if(result->data.size>=pageSize) { // Remaps file read-only (will be remapped Read|Write whenever used as output again)
                result->maps << Map(file);
                result->data = buffer<byte>(result->maps.last());
            }
        }
        result->rename();
        assert_(existsFile(result->fileName,storageFolder), result->fileName);
        results << move(result);
    }
    return share(find(target, arguments));
}
