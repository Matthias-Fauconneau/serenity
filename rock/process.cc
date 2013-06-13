#include "process.h"
#include "data.h"
#include "time.h"

array<ref<byte> > Rule::parameters() const {
    array<ref<byte>> parameters;
    if(this->operation) {
        unique<Operation> operation = Interface<Operation>::instance(this->operation);
        assert_(operation, "Operation", this->operation, "not found in", Interface<Operation>::factories.keys);
        parameters += split(operation->parameters());
    }
    for(auto arg: argumentExps) if(arg.value.type==Expression::Value) parameters += arg.value;
    return parameters;
}
/// Evaluates local rule arguments (using scope for value arguments)
Dict Rule::arguments(const Dict& scopeArguments, const ref<byte>& scope) const {
    Dict arguments;
    for(auto arg: argumentExps) {
        assert_(parameters().contains(arg.key), "Irrelevant argument", arg.key, "for", *this);
        ref<byte> value = arg.value;
        if(arg.value.type == Expression::Value) {
            assert_(scopeArguments.contains(value), scope+"/"_+str(*this), ": Undefined", value, "in evaluation of", arg.key, "with scope arguments", scopeArguments);
            value = scopeArguments.at(value);
        }
        arguments.insert(copy(arg.key), copy(value));
    }
    return arguments;
}

array<ref<byte> > Process::parameters() {
    array<ref<byte>> parameters = copy(specialParameters); // Valid parameters accepted by operations compiled in this binary, used in process definition or for derived class special behavior
    for(auto factory: Interface<Operation>::factories.values) parameters += split(factory->constructNewInstance()->parameters());
    return parameters;
}

array<ref<byte> > Process::configure(const ref<ref<byte> >& allArguments, const ref<byte>& definition) {
    array<ref<byte>> targets, specialArguments;
    // Parses definitions and arguments twice to solve cyclic dependencies
    // 1) Process definition defines valid parameters and targets
    // 2) Arguments are parsed using default definition
    // 3) Process definition is parsed with conditionnals taken according to user arguments
    // 4) Arguments are parsed again using the customized process definition
    for(uint pass unused: range(2)) {
        rules.clear(); resultNames.clear(); array<ref<byte>> sweepOverrides;
        array<ref<byte>> parameters = this->parameters();
        Dict defaultArguments; // Process-specified default arguments
        map<ref<byte>, array<Variant>> defaultSweeps; // Process-specified default sweeps

        for(TextData s(definition); s;) { //FIXME: factorize (parseArgument, parseSweep, ...), use parser
            s.skip();
            if(s.match('#')) { s.until('\n'); continue; }
            if(s.match("if"_)) {
                s.skip();
                bool enable;
                if(s.match('!')) {
                    ref<byte> parameter = s.word("_-"_);
                    parameters += parameter;
                    enable = !arguments.contains(parameter);
                } else {
                    ref<byte> parameter = s.word("_-"_);
                    parameters += parameter;
                    s.skip();
                    if(s.match("=="_)) {
                        s.skip();
                        s.skip("'"_);
                        ref<byte> literal = s.until('\'');
                        enable = arguments.value(parameter, "0"_) == literal;
                    } else {
                        enable = arguments.contains(parameter);
                    }
                }
                s.skip(":"_);
                if(!enable) { s.until('\n'); continue; }
                s.skip();
            }
            array<ref<byte>> outputs;
            for(;!s.match('='); s.skip()) {
                ref<byte> output = s.word("_-"_);
                assert_(output, s.until('\n'));
                outputs << output;
            }
            assert_(outputs, s.until('\n'));
            s.skip();
            if(outputs.size==1 /*&& parameters.contains(outputs[0]) process parameter might not be defined yet*/ && (s.peek()=='\'' || s.peek()=='{')) { // Default argument
                ref<byte> key = outputs[0];
                if(s.match('\'')) {
                    ref<byte> value = s.until('\''); // Literal
                    assert_(!defaultArguments.contains(key),"Multiple default argument definitions for",key);
                    defaultArguments.insert(key, value);
                    if(!arguments.contains(key)) arguments.insert(key, value);
                }
                else if(s.match('{')) { // Sweep
                    ref<byte> sweep = s.until('}');
                    assert(!arguments.contains(key));
                    array<Variant> sequence;
                    if(::find(sweep,".."_)) {
                        TextData s (sweep);
                        int begin = s.integer(); s.skip(".."_); int end = s.integer(); assert(!s);
                        assert(begin >= 0 && end > begin);
                        for(uint i: range(begin, end+1)) sequence << i;
                    } else sequence = apply<Variant>(split(sweep,','), [](const ref<byte>& o){return o;});
                    defaultSweeps.insert(key, sequence);
                    if(!sweeps.contains(key)) sweeps.insert(key, sequence);
                }
                // TODO: default argument value
                else error("Unquoted literal", key, s.whileNo(" \t\r\n"_));
            } else {
                Rule rule;
                ref<byte> word = s.word("_-"_);
                assert_(word, "Expected operator or input for", outputs);
                if(!Interface<Operation>::factories.contains(word)) rule.inputs << word; // Forwarding rule
                else rule.operation = word; // Generating rule
                s.whileAny(" \t\r"_);
                for(;!s.match('\n'); s.whileAny(" \t\r"_)) {
                    if(s.match('#')) { s.whileNot('\n'); continue; }
                    ref<byte> key = s.word("_-"_);
                    assert_(key, s.until('\n'), word);
                    if(s.match('=')) { // Explicit argument
                        if(s.match('{')) { // Sweep
                            ref<byte> sweep = s.until('}');
                            assert(!arguments.contains(key));
                            if(sweeps.contains(key)) { // User overrides local sweep
                                rule.sweeps.insert(key, sweeps.at(key));
                                if(targets.contains(rule.inputs[0])) sweepOverrides += key;
                            } else {
                                if(::find(sweep,".."_)) {
                                    TextData s (sweep);
                                    int begin = s.integer(); s.skip(".."_); int end = s.integer(); assert(!s);
                                    assert(begin >= 0 && end > begin);
                                    array<Variant> sequence;
                                    for(uint i: range(begin, end+1)) sequence << i;
                                    rule.sweeps.insert(key, sequence);
                                } else rule.sweeps.insert(key, apply<Variant>(split(sweep,','), [](const ref<byte>& o){return o;}));
                            }
                        } else {
                            if(s.match('\'')) rule.argumentExps.insert(key, Rule::Expression(s.until('\'')));
                            else {
                                ref<byte> word = s.word("-_"_);
                                assert_(word, "Unquoted literal", s.whileNo(" \t\r\n"_));
                                parameters += word;
                                rule.argumentExps.insert(key, Rule::Expression(word, Rule::Expression::Value));
                            }
                        }
                    }
                    else if(resultNames.contains(key)) rule.inputs << key; // Result input
                    else if(parameters.contains(key)) {
                        if(arguments.contains(key)) rule.inputs << key; // Argument value
                        else if(sweeps.contains(key)) rule.sweeps.insert(key, copy(sweeps.at(key))); // Sweep value
                        else rule.argumentExps.insert(key); // Empty argument
                    } else parameters += key, rule.inputs << key; // Argument value (yet undefined)
                }
                for(ref<byte> output: outputs) { assert_(!resultNames.contains(output), "Multiple result definitions for", output); resultNames << output; }
                rule.outputs = move(outputs);
                rules << move(rule);
            }
        }

        arguments.clear(); sweeps.clear(); targets.clear(); specialArguments.clear();
        for(const ref<byte>& argument: allArguments) { // Parses generic arguments (may affect process definition)
            TextData s (argument); ref<byte> key = s.word("-_"_);
            if(s.match('=')) { // Explicit argument
                assert_(parameters.contains(key),"Invalid parameter", key);
                ref<byte> value = s.untilEnd();
                if(sweeps.contains(key) || arguments.contains(key)) {
                    if(arguments.contains(key)) sweeps.insert(key, array<Variant>()<<arguments.take(key));
                    sweeps.at(key) << value;
                } else {
                    arguments.insert(key, Variant(value));
                }
            }
            else if(resultNames.contains(argument)) targets << argument;
            else if(parameters.contains(argument)) arguments.insert(argument,""_);
            else specialArguments << argument;
        }
        for(auto arg: defaultArguments) if(!arguments.contains(arg.key)) arguments.insert(arg.key, arg.value);
        for(auto arg: defaultSweeps) if(!sweeps.contains(arg.key)) sweeps.insert(arg.key, arg.value);
        for(auto key: sweeps.keys) assert_(!arguments.contains(key));
        for(ref<byte> key: sweepOverrides) sweeps.remove(key); // Removes sweep overrides from process sweeps
    }
    this->parseSpecialArguments(specialArguments);
    return targets;
}

Rule& Process::ruleForOutput(const ref<byte>& target) { for(Rule& rule: rules) for(const ref<byte>& output: rule.outputs) if(output==target) return rule; return *(Rule*)0; }

Dict Process::relevantArguments(const ref<byte>& target, const Dict& arguments, const ref<byte>& scope) {
    const Rule& rule = ruleForOutput(target);
    Dict relevant;
    if(!&rule && arguments.contains(target)) { // Conversion from argument to result
        relevant.insert(target, arguments.at(target));
        return relevant;
    }
    assert_(&rule, "No rule generating '"_+target+"' for"_,scope);

    Dict args = copy(arguments);
    for(auto arg: rule.sweeps) {
        if(args.contains(arg.key)) /*log("Sweep overrides scope argument",arg.key,'=',args.at(arg.key),"->"_,arg.value),*/ args.remove(arg.key);
        args.insert(arg.key, str(arg.value,','));
    }

    // Input argument are also relevant in case they are deleted
    for(const ref<byte>& input: rule.inputs) {
        for(auto arg: relevantArguments(input, args, scope+"/"_+target)) { //FIXME: memoize
            if(relevant.contains(arg.key)) assert_(relevant.at(arg.key)==arg.value, "Arguments conflicts", arg.key, relevant.at(arg.key), arg.value);
            else relevant.insert(move(arg.key), move(arg.value));
        }
    }
    if(rule.operation) {
        Dict local = copy(rule.arguments(args, scope)); // not inherited
        for(auto arg: args) {
            //if(local.contains(arg.key)) /*log("Scope overrides local argument",arg.key,'=',local.at(arg.key),"->"_,arg.value),*/ local.remove(arg.key);
            local.insert(copy(arg.key), copy(arg.value));
        }
        for(ref<byte> parameter: rule.parameters()) if(local.contains(parameter)) {
            if(relevant.contains(parameter)) assert_(relevant.at(parameter) == local.at(parameter), "Arguments conflicts", parameter, relevant.at(parameter), local.at(parameter));
            else relevant.insert(parameter, local.at(parameter));
        }
    }
    return relevant;
}

int Process::indexOf(const ref<byte>& target, const Dict& arguments) {
    Dict relevantArguments = Process::relevantArguments(target, arguments);
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
    if(!&rule && arguments.contains(target)) return true; // Conversion from argument to result
    assert_(&rule);
    for(const ref<byte>& input: rule.inputs) if(!sameSince(input, queryTime, arguments)) return false; // Inputs changed since result (or query if result was discarded) was last generated
    if(rule.operation && parse(Interface<Operation>::version(rule.operation))*1000000000l > queryTime) return false; // Implementation changed since query
    return true;
}

shared<Result> Process::getResult(const ref<byte>& target, const Dict& arguments) {
    const shared<Result>& result = find(target, arguments);
    if(&result && sameSince(target, result->timestamp, arguments)) { assert(result->data.size); return share(result); }
    error("Anonymous process manager unimplemented"_);
}

void Process::execute(const ref<ref<byte> >& targets, const map<ref<byte>, array<Variant>>& sweeps, const Dict& arguments) {
    if(sweeps) {
        map<ref<byte>, array<Variant>> remaining = copy(sweeps);
        Dict args = copy(arguments);
        ref<byte> parameter = sweeps.keys.first(); // Removes first parameter and loop over it
        //assert_(!args.contains(parameter), "Sweep parameter overrides existing argument", args.at(parameter));
        if(args.contains(parameter)) args.remove(parameter); // Allows sweep to override default arguments
        for(Variant& value: remaining.take(parameter)) {
            args.insert(parameter, move(value));
            execute(targets, remaining, args);
            args.remove(parameter);
        }
    } else { // Actually generates targets when sweeps have been explicited
        assert_(targets, "Expected target, got only arguments:", arguments);
        for(const ref<byte>& target: targets) {
            log(">>", target, relevantArguments(target, arguments));
            Time time;
            targetResults << getResult(target, arguments);
            if((uint64)time > 100) log("<<", target, time);
        }
    }
}

void Process::execute(const ref<ref<byte> >& allArguments, const ref<byte>& definition) {
    targetResults.clear();
    execute(configure(allArguments, definition), sweeps, arguments);
}

void PersistentProcess::parseSpecialArguments(const ref<ref<byte> >& args) {
    if(!name) name = string(args.first()); // Use first special argument as storage folder name (if not already defined by derived class)
    if(arguments.contains("baseStorageFolder"_)) baseStorageFolder = Folder(arguments.at("baseStorageFolder"_),currentWorkingDirectory());
    storageFolder = Folder(name, baseStorageFolder, true);

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
    const Rule& rule = ruleForOutput(target);
    if(!&rule && arguments.contains(target)) return shared<ResultFile>(target, 0, Dict(), string(), copy(arguments.at(target)), ""_, ""_); // Conversion from argument to result
    assert_(&rule, target);

    const shared<Result>& result = find(target, arguments);
    if(&result && sameSince(target, result->timestamp, arguments)) return share(result); // Returns a cached result if still valid
    // Otherwise regenerates target using new inputs, arguments and/or implementations

    array<shared<Result>> inputs;
    if(!rule.sweeps)
        for(const ref<byte>& input: rule.inputs) inputs << getResult(input, arguments);
    for(const shared<Result>& input: inputs) {
        ResultFile& result = *(ResultFile*)input.pointer;
        if(result.fileName) touchFile(result.fileName, result.folder, false); // Updates last access time for correct LRU cache behavior
    }

    if(!rule.operation && !rule.sweeps) { // Simple forwarding rule
        assert(inputs.size == 1 && rule.outputs.size==1, "FIXME: Only single inputs can be forwarded");
        return move(inputs.first());
    }
    unique<Operation> operation = rule.operation ? Interface<Operation>::instance(rule.operation) : nullptr;

    array<shared<Result>> outputs;
    for(uint index: range(rule.outputs.size)) {
        const ref<byte>& output = rule.outputs[index];

        if(&find(output, arguments)) { // Reuses same result file
            shared<ResultFile> result = results.take(indexOf(output, arguments));
            string fileName = move(result->fileName); assert(!result->fileName.size);
            rename(fileName, output, storageFolder);
        }

        Map map;
        int64 outputSize = operation ? operation->outputSize(arguments, cast<Result*>(inputs), index) : 0;
        if(outputSize) { // Creates (or resizes) and maps an output result file
            while(outputSize > (existsFile(output, storageFolder) ? File(output, storageFolder).size() : 0) + freeSpace(storageFolder)) {
                long minimum=realTime(); string oldest;
                for(string& path: baseStorageFolder.list(Files|Recursive)) { // Discards oldest unused result (across all process hence the need for ResultFile's inter process reference counter)
                    TextData s (path); s.until('}'); int userCount=s.mayInteger(); if(userCount>1 || !s.match('.')) continue; // Used data or not a process data
                    long timestamp = File(path, baseStorageFolder).accessTime();
                    if(timestamp < minimum) minimum=timestamp, oldest=move(path);
                }
                if(!oldest) { if(outputSize<=1l<<32) error("Not enough space available"); else break; /*Virtual*/ }
                if(section(oldest,'/')==name) {
                    TextData s (section(oldest,'/',1,-1)); ref<byte> name = s.whileNot('{'); Dict relevantArguments = parseDict(s);
                    for(uint i: range(results.size)) if(results[i]->name==name && results[i]->relevantArguments==relevantArguments) {
                        ((shared<ResultFile>)results.take(i))->fileName.clear(); // Prevents rename
                        break;
                    }
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

    log_(str(rule));
    Time time;
    Dict relevantArguments;
    if(operation) {
        relevantArguments = Process::relevantArguments(target, arguments);
        log_("\t"_+str(relevantArguments));
        operation->execute(relevantArguments, cast<Result*>(outputs), cast<Result*>(inputs));
    } else { // Sweep generator
           assert_(rule.sweeps.size()==1, "FIXME: Only single sweeps can be generated");
           assert_(rule.inputs.size == 1 && outputs.size==1, "FIXME: Only single target sweeps can be generated");

           Dict args = copy(arguments);
           ref<byte> parameter = rule.sweeps.keys.first(); // Removes first parameter and loop over it
           if(args.contains(parameter)) args.remove(parameter); // Sweep overrides (default) argument

           args.insert(parameter, str(rule.sweeps.at(parameter)));
           relevantArguments = Process::relevantArguments(target, arguments);
           assert_(relevantArguments.contains(parameter), "Irrelevant sweep parameter", parameter, "for", rule.inputs.first(), relevantArguments);
           log_("\t"_+str(relevantArguments));

           string data;
           for(const Variant& value: rule.sweeps.at(parameter)) {
               args.remove(parameter);
               args.insert(parameter, value);
               shared<Result> result = getResult(rule.inputs.first(), args);
               assert_(result->metadata=="scalar"_, "FIXME: only scalar sweep can be generated"_);
               data << result->relevantArguments.at(parameter) << "\t"_ << result->data;
           }
           outputs.first()->metadata = string("sweep.tsv"_);
           outputs.first()->data = move(data);
    }
    log("\t"_+str(time));

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
