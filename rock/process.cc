#include "process.h"
#include "data.h"
#include "time.h"

array<ref<byte> > Process::parameters() {
    array<ref<byte>> parameters = copy(specialParameters); // Valid parameters accepted by operations compiled in this binary, used in process definition or for derived class special behavior
    for(auto factory: Interface<Operation>::factories.values) parameters += split(factory->constructNewInstance()->parameters());
    return parameters;
}

array<ref<byte> > Process::configure(const ref<ref<byte> >& allArguments, const ref<byte>& definition) {
    array<ref<byte>> targets, specialArguments;
    array<array<ref<byte>>> results; // Intermediate result names for each target
    array<array<ref<byte>>> sweepOverrides; // Sweep overrides for each target (Converts user specified sweeps to rule sweeps)
    Dict defaultArguments; // Process-specified default arguments
    Sweeps sweeps, defaultSweeps; // Process-specified default sweeps
    // Parses definitions and arguments twice to solve cyclic dependencies
    // 1) Process definition defines valid parameters and targets
    // 2) Arguments are parsed using default definition
    // 3) Process definition is parsed with conditionnals taken according to user arguments
    // 4) Arguments are parsed again using the customized process definition
    for(uint pass unused: range(2)) {
        array<ref<byte>> parameters = this->parameters();
        rules.clear(); resultNames.clear(); sweepOverrides.clear(); sweepOverrides.grow(targets.size); defaultArguments.clear(); defaultSweeps.clear();

        for(TextData s(definition); s;) { //FIXME: factorize (parseArgument, parseSweep, ...), use parser
            s.skip();
            if(s.match('#')) { s.until('\n'); continue; }
            if(s.match("if"_)) {
                s.skip();
                bool enable;
                ref<byte> op;
                ref<byte> right;
                if(s.match('!')) op="=="_, right="0"_;
                ref<byte> parameter = s.word("_-"_);
                parameters += parameter;
                Variant left = arguments.value(parameter, "0"_);
                if(!left) left="1"_;
                s.skip();
                if(!op) op = s.whileAny("!="_);
                if(!op) op = "!="_, right = "0"_;
                if(!right) { s.skip(); s.skip("'"_); right = s.until('\''); }
                assert_(left && right);
                if(op=="=="_) enable = (left == right);
                else if(op=="!="_) enable = (left != right);
                else error("Unknown operator", op);
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
                parameters += key; // May not be defined yet
                if(s.match('\'')) {
                    ref<byte> value = s.until('\''); // Literal
                    assert_(!defaultArguments.contains(key),"Multiple default argument definitions for",key);
                    defaultArguments.insert(string(key), Variant(value));
                    if(!arguments.contains(key)) arguments.insert(string(key), Variant(value));
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
                    assert_(sequence);
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
                                assert_(sweeps.at(key));
                                rule.sweeps.insert(key, sweeps.at(key));
                                for(uint i: range(targets.size)) if(results[i].contains(rule.inputs[0])) sweepOverrides[i] += key;
                            } else {
                                if(::find(sweep,".."_)) {
                                    TextData s (sweep);
                                    int begin = s.integer(); s.skip(".."_); int end = s.integer(); assert(!s);
                                    assert(begin >= 0 && end > begin);
                                    array<Variant> sequence;
                                    for(uint i: range(begin, end+1)) sequence << i;
                                    assert_(sequence);
                                    rule.sweeps.insert(key, sequence);
                                } else rule.sweeps.insert(key, apply<Variant>(split(sweep,','), [](const ref<byte>& o){return o;}));
                            }
                        } else {
                            if(s.match('\'')) rule.argumentExps.insert(string(key), Rule::Expression(s.until('\'')));
                            else {
                                ref<byte> word = s.word("-_"_);
                                assert_(word, "Unquoted literal", s.whileNo(" \t\r\n"_));
                                parameters += word;
                                rule.argumentExps.insert(string(key), Rule::Expression(word, Rule::Expression::Value));
                            }
                        }
                    }
                    else if(resultNames.contains(key)) rule.inputs << key; // Result input
                    else {
                        if(rule.operation) rule.inputs << key; // Argument value
                        else if(sweeps.contains(key)) {
                            assert_(sweeps.at(key), sweeps);
                            rule.sweeps.insert(key, copy(sweeps.at(key))); // Sweep value
                            for(uint i: range(targets.size)) if(results[i].contains(outputs[0])) sweepOverrides[i] += key;
                        } else if(pass==2) error(key, arguments);
                    }
                }
                for(ref<byte> output: outputs) { assert_(!resultNames.contains(output), "Multiple result definitions for", output); resultNames << output; }
                rule.outputs = move(outputs);
                rules << move(rule);
            }
        }

        arguments.clear(); targetsSweeps.clear(); sweeps.clear(); targets.clear(); results.clear(); specialArguments.clear();
        for(const ref<byte>& argument: allArguments) { // Parses generic arguments (may affect process definition)
            TextData s (argument); ref<byte> key = s.word("-_."_);
            if(s.match('=')) { // Explicit argument
                ref<byte> scope, parameter = key;
                if(key.contains('.')) scope=section(key, '.', 0, 1), parameter=section(key, '.', 1, 2);
                assert_(parameters.contains(parameter),"Invalid parameter", parameter);
                ref<byte> value = s.untilEnd();
                assert_(value);
                if(scope) {
                    bool match=false;
                    for(Rule& rule: rules) for(const ref<byte>& output: rule.outputs) if(startsWith(output, scope)) {
                        match = true;
                        rule.argumentExps[parameter] = Rule::Expression(value);
                    }
                    assert_(match, scope);
                } else if(arguments.contains(parameter)) { Variant value=arguments.take(parameter); assert_(value); sweeps.insert(parameter, array<Variant>()<<value); }
                else if(sweeps.contains(parameter)) sweeps.at(parameter) << value;
                else arguments.insert(string(parameter), Variant(value));
            }
            else if(resultNames.contains(argument)) targets << argument;
            else if(parameters.contains(argument)) arguments.insert(string(argument), ""_);
            else specialArguments << argument;
        }
        for(auto arg: defaultArguments) if(!arguments.contains(arg.key)) arguments.insert(arg.key, arg.value);
        for(auto arg: defaultSweeps) { assert_(arg.value); if(!sweeps.contains(arg.key)) sweeps.insert(arg.key, arg.value); }
        results.grow(targets.size);
        for(uint i: range(targets.size)) {
            array<ref<byte>> stack; stack << targets[i];
            while(stack) { // Traverse dependency to get all intermediate result names
                ref<byte> result = stack.pop();
                results[i] += result;
                const Rule& rule = ruleForOutput(result);
                if(&rule) stack << rule.inputs;
            }
        }
    }
    for(auto key: sweeps.keys) if(!defaultArguments.contains(key)) assert_(!arguments.contains(key), "Sweep overrides argument", key, defaultArguments);
    for(uint i: range(targets.size)) {
        Sweeps targetSweeps;
        for(auto sweep: (const Sweeps&)sweeps) { // Discards irrelevant sweeps
            assert_(sweep.value, targets[i], sweeps);
            Dict args = copy(arguments);
            args.insert(string(sweep.key), str(sweep.value,','));
            bool relevant = false;
            if(evaluateArguments(targets[i],args).contains(sweep.key)) relevant=true;
            if(relevant) targetSweeps.insert(sweep.key, sweep.value);
            else assert_(defaultSweeps.contains(sweep.key), "Irrelevant sweep parameter", sweep.key);
        }
        for(ref<byte> key: sweepOverrides[i]) targetSweeps.remove(key); // Removes sweep overrides from process sweeps
        targetsSweeps << targetSweeps;
    }
    this->parseSpecialArguments(specialArguments);
    return targets;
}

Rule& Process::ruleForOutput(const ref<byte>& target) { for(Rule& rule: rules) for(const ref<byte>& output: rule.outputs) if(output==target) return rule; return *(Rule*)0; }

Dict Process::evaluateArguments(const ref<byte>& target, const Dict& scopeArguments, bool local, const ref<byte>& scope) {
    const Rule& rule = ruleForOutput(target);
    Dict args;
    if(!&rule && scopeArguments.contains(target)) { // Conversion from argument to result
        args.insert(string(target), scopeArguments.at(target));
        return args;
    }
    assert_(&rule, "No rule generating '"_+target+"'"_, scope, scopeArguments);

    Dict scopeArgumentsAndSweeps = copy(scopeArguments);
    for(auto arg: rule.sweeps) {
        assert_(arg.value, arg.key);
        //if(scopeArgumentsAndSweeps.contains(arg.key)) assert_(scopeArgumentsAndSweeps.at(arg.key)==str(arg.value,','), rule, arg.key, scopeArgumentsAndSweeps.at(arg.key), str(arg.value,','));
        if(scopeArgumentsAndSweeps.contains(arg.key)) {}
        else scopeArgumentsAndSweeps.insert(string(arg.key), str(arg.value,',')); // Explicits sweep as arguments (for argument validation)
    }

    // Recursively evaluate to invalid cache on argument changes
    for(const ref<byte>& input: rule.inputs) {
        for(auto arg: evaluateArguments(input, scopeArgumentsAndSweeps, false, scope+"/"_+target)) { //FIXME: memoize
            if(args.contains(arg.key)) assert_(args.at(arg.key)==arg.value);
            else args.insert(arg.key, arg.value);
        }
    }

    /// Relevant rule parameters (from operation and argument expressions)
    array<ref<byte>> parameters;
    if(rule.operation) {
        unique<Operation> operation = Interface<Operation>::instance(rule.operation);
        assert_(operation, "Operation", rule.operation, "not found in", Interface<Operation>::factories.keys);
        parameters += split(operation->parameters());
    }
    for(auto arg: rule.argumentExps) if(arg.value.type==Rule::Expression::Value) parameters += arg.value;

    for(auto arg: scopeArguments) if(parameters.contains(arg.key)) {
        if(args.contains(arg.key)) assert_(args.at(arg.key)==arg.value);
        else args.insert(copy(arg.key), copy(arg.value)); // Filters relevant scope arguments
    }
    for(auto arg: rule.sweeps) { // Explicits sweep as arguments (for cache validation)
        assert_(arg.value, arg.key);
        if(args.contains(arg.key)) assert_(args.at(arg.key)==str(arg.value,','), rule, arg.key, "'"_+args.at(arg.key)+"'"_, "'"_+str(arg.value,',')+"'"_);
        else args.insert(string(arg.key), str(arg.value,','));
    }
    for(auto arg: rule.argumentExps) { // Evaluates local arguments
        if(args.contains(arg.key)) continue;
        assert_(parameters.contains(arg.key), "Irrelevant argument", arg.key, "for", rule);
        if(arg.value.type == Rule::Expression::Value) { // Local value argument
            assert_(scopeArguments.contains(arg.value), rule, ": Undefined", arg.value);
            args.insert(copy(arg.key), copy(scopeArguments.at(arg.value)));
        } else if(local) { // Local literal argument (only needed for Operation execution)
            args.insert(copy(arg.key), arg.value);
        }
    }
    return args;
}

int Process::indexOf(const ref<byte>& target, const Dict& arguments) {
    Dict relevantArguments = Process::evaluateArguments(target, arguments);
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
    for(const ref<byte>& input: rule.inputs) { // Inputs changed since result (or query if result was discarded) was last generated
        if(rule.sweeps) { // Check all sweeped results
            assert_(rule.sweeps.size()==1);
            Dict args = copy(arguments);
            ref<byte> parameter = rule.sweeps.keys.first(); // Removes first parameter and loop over it
            if(args.contains(parameter)) args.remove(parameter); // Sweep overrides (default) argument
            for(const Variant& value: rule.sweeps.at(parameter)) {
                args.insert(string(parameter), value);
                if(!sameSince(rule.inputs.first(), queryTime, args)) return false;
                args.remove(parameter);
            }
        } else {
            if(!sameSince(input, queryTime, arguments)) return false;
        }
    }
    if(rule.operation && parse(Interface<Operation>::version(rule.operation))*1000000000l > queryTime) return false; // Implementation changed since query
    return true;
}

shared<Result> Process::getResult(const ref<byte>& target, const Dict& arguments) {
    const shared<Result>& result = find(target, arguments);
    if(&result && sameSince(target, result->timestamp, arguments)) { assert(result->data.size); return share(result); }
    error("Anonymous process manager unimplemented"_);
}

void Process::execute(const ref<byte>& target, const Sweeps& sweeps, const Dict& arguments) {
    if(sweeps) {
        Sweeps remaining = copy(sweeps);
        ref<byte> parameter = sweeps.keys.first(); // Removes first parameter and loop over it
        array<Variant> sweep = remaining.take(parameter);
        Dict args = copy(arguments);
        //assert_(!args.contains(parameter), "Sweep parameter overrides existing argument", args.at(parameter));
        //if(args.contains(parameter)) args.remove(parameter); // Allows sweep to override default arguments
        args.insert(string(parameter), str(sweep,','));
        if(!evaluateArguments(target,args).contains(parameter)) return execute(target, remaining, args);
        assert_(evaluateArguments(target,args).contains(parameter), "Irrelevant sweep parameter", parameter);
        for(Variant& value: sweep) {
            args.remove(parameter);
            args.insert(string(parameter), move(value));
            execute(target, remaining, args);
        }
    } else { // Actually generates targets when sweeps have been explicited
        log(">>", target, evaluateArguments(target, arguments));
        Time time;
        targetResults << getResult(target, arguments);
        if((uint64)time > 100) log("<<", target, time);
    }
}

void Process::execute(const ref<ref<byte> >& allArguments, const ref<byte>& definition) {
    targetResults.clear();
    array<ref<byte>> targets = configure(allArguments, definition);
    for(uint i: range(targets.size)) execute(targets[i], targetsSweeps[i], arguments);
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
    if(!rule.sweeps) for(const ref<byte>& input: rule.inputs) inputs << getResult(input, arguments);
    for(const shared<Result>& input: inputs) {
        ResultFile& result = *(ResultFile*)input.pointer;
        if(result.fileName) touchFile(result.fileName, result.folder, false); // Updates last access time for correct LRU cache behavior
    }

    if(!rule.operation && !rule.sweeps) { // Simple forwarding rule
        assert(inputs.size == 1 && rule.outputs.size==1, "FIXME: Only single inputs can be forwarded");
        return move(inputs.first());
    }

    unique<Operation> operation = !rule.sweeps ? Interface<Operation>::instance(rule.operation) : nullptr;

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

    Time time;
    Dict relevantArguments;
    if(operation) {
        relevantArguments = evaluateArguments(target, arguments);
        operation->execute(evaluateArguments(target, arguments, true), cast<Result*>(outputs), cast<Result*>(inputs));
    } else { // Sweep generator
           assert_(rule.sweeps.size()==1, "FIXME: Only single sweeps can be generated");
           assert_(rule.inputs.size == 1 && outputs.size==1, "FIXME: Only single target sweeps can be generated");

           Dict args = copy(arguments);
           ref<byte> parameter = rule.sweeps.keys.first(); // Removes first parameter and loop over it
           if(args.contains(parameter)) args.remove(parameter); // Sweep overrides (default) argument

           args.insert(string(parameter), str(rule.sweeps.at(parameter),','));
           relevantArguments = evaluateArguments(target, arguments);
           assert_(relevantArguments.contains(parameter), "Irrelevant sweep parameter", parameter, "for", rule.inputs.first(), relevantArguments);

           string data;
           for(const Variant& value: rule.sweeps.at(parameter)) {
               args.remove(parameter);
               args.insert(string(parameter), value);
               shared<Result> result = getResult(rule.inputs.first(), args);
               assert_(result->metadata=="scalar"_, "Non-scalar sweep for"_, rule, rule.sweeps);
               data << result->relevantArguments.at(parameter) << "\t"_ << result->data;
           }
           outputs.first()->metadata = string("sweep.tsv"_);
           outputs.first()->data = move(data);
    }
    /*if((uint64)time>100)*/ log(rule, relevantArguments, time);

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
            if(mappedSize && result->data.size <= mappedSize) { // Truncates file to result size
                file = File(result->fileName, result->folder, ReadWrite);
                if(result->data.size < mappedSize) file.resize(result->data.size);
                //else only open file to map read-only
            } else { // Copies data from anonymous memory to file
                file = File(result->fileName, result->folder, Flags(ReadWrite|Truncate|Create));
                file.write(result->data);
            }
            if(result->data.size>=pageSize) { // Remaps file read-only (will be remapped Read|Write whenever used as output again)
                result->maps << Map(file);
                result->data = buffer<byte>(result->maps.last());
            }
        }
        results << move(result);
    }
    return share(find(target, arguments));
}
