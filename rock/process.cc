#include "process.h"
#include "data.h"
#include "time.h"
#include "math.h"

array<string> Process::parameters() {
    array<string> parameters = copy(specialParameters);
    for(auto factory: Interface<Operation>::factories.values) parameters += split(factory->constructNewInstance()->parameters());
    return parameters;
}

array<string> Process::configure(const ref<string>& allArguments, const string& definition) {
    array<string> targets;
    array<array<string>> results; // Intermediate result names for each target
    //array<array<string>> sweepOverrides; // Sweep overrides for each target (Converts user specified sweeps to rule sweeps)
    Dict defaultArguments; // Process-specified default arguments
    Sweeps sweeps, defaultSweeps; // Process-specified default sweeps
    // Parses definitions and arguments twice to solve cyclic dependencies
    // 1) Process definition defines valid parameters and targets
    // 2) Arguments are parsed using default definition
    // 3) Process definition is parsed with conditionnals taken according to user arguments
    // 4) Arguments are parsed again using the customized process definition
    for(uint pass unused: range(2)) {
        array<string> parameters = this->parameters();
        rules.clear(); resultNames.clear(); /*sweepOverrides.clear(); sweepOverrides.grow(targets.size);*/ defaultArguments.clear(); defaultSweeps.clear();

        for(TextData s(definition); s;) { //FIXME: factorize (arguments, sweeps, expressions ...), use parser
            s.skip();
            if(s.match('#')) { s.until('\n'); continue; }
            if(s.match("if"_)) {
                s.whileAny(" \t\r"_);
                bool enable;
                string op;
                string right;
                if(s.match('!')) op="=="_, right="0"_;
                string parameter = s.word("_-"_);
                parameters += parameter;
                String left ("0"_);
                if(sweeps.contains(parameter)) left = str(sweeps.at(parameter),',');
                else if(arguments.contains(parameter)) left = copy(arguments.at(parameter));
                if(!left) left=String("1"_);
                s.whileAny(" \t\r"_);
                if(!op) op = s.whileAny("!="_);
                if(!op) op = "!="_, right = "0"_;
                if(!right) { s.whileAny(" \t\r"_); s.skip("'"_); right = s.until('\''); }
                assert_(left && right);
                if(op=="=="_) enable = (left == right);
                else if(op=="!="_) enable = (left != right);
                else error("Unknown operator", op);
                s.skip(":"_);
                if(!enable) { s.until('\n'); continue; }
                s.whileAny(" \t\r"_);
            }
            array<string> outputs;
            for(;!s.match('='); s.whileAny(" \t\r"_)) {
                string output = s.word("_-"_);
                assert_(output, s.until('\n'));
                outputs << output;
            }
            assert_(outputs, s.until('\n'));
            s.whileAny(" \t\r"_);
            if(outputs.size==1 && (s.peek()=='\'' || s.peek()=='{' || s.peek()=='$')) { // Default argument
                string key = outputs[0];
                parameters += key; // May not be defined yet
                if(s.match('\'')) {
                    string value = s.until('\''); // Literal
                    assert_(!defaultArguments.contains(key),"Multiple default argument definitions for",key);
                    defaultArguments.insert(String(key), String(value));
                    if(!arguments.contains(key)) arguments.insert(String(key), String(value));
                }
                else if(s.match('{')) { // Sweep
                    string sweep = s.until('}');
                    //assert_(!arguments.contains(key));
                    array<Variant> sequence;
                    if(::find(sweep,".."_)) {
                        TextData s (sweep);
                        int begin = s.integer(); s.skip(".."_); int end = s.integer(); assert(!s);
                        assert(begin >= 0 && end > begin);
                        for(uint i: range(begin, end+1)) sequence << i;
                    } else sequence = apply<Variant>(split(sweep,','), [](const string& o){return String(o);});
                    assert_(sequence);
                    defaultSweeps.insert(String(key), copy(sequence));
                    if(!sweeps.contains(key)) sweeps.insert(String(key), copy(sequence));
                } else if(s.match('$')) { // Default argument value
                    string word = s.word("_-"_);
                    assert_(word);
                    string op = s.whileAny("^"_);
                    int right=0;
                    if(op) right = s.integer();
                    if(sweeps.contains(word)) {
                        array<Variant> values;
                        for(const Variant& left: sweeps.take(word)) {
                            Variant value;
                            if(!op) value = copy(left);
                            else if(op=="^"_) value = pow(left, right);
                            else error("Unknown operator", op);
                            values << move(value);
                        }
                        defaultSweeps.insert(String(key), copy(values));
                        if(!sweeps.contains(key)) sweeps.insert(String(key), copy(values));
                    } else {
                        const Variant& left = arguments.at(word);
                        Variant value;
                        if(!op) value = copy(left);
                        else if(op=="^"_) value = pow(left, right);
                        else error("Unknown operator", op);
                        defaultArguments.insert(String(key), copy(value));
                        if(!arguments.contains(key)) arguments.insert(String(key), copy(value));
                    }
                }
                //else error("Unquoted literal", key, s.whileNo(" \t\r\n"_));
            } else {
                Rule rule;
                string word = s.word("_-"_);
                assert_(word, "Expected operator or input for", outputs);
                if(!Interface<Operation>::factories.contains(word)) rule.inputs << word; // Forwarding rule
                else rule.operation = word; // Generating rule
                s.whileAny(" \t\r"_);
                for(;!s.match('\n'); s.whileAny(" \t\r"_)) {
                    if(s.match('#')) { s.whileNot('\n'); continue; }
                    string key = s.word("_-"_); s.whileAny(" \t\r"_);
                    assert_(key, s.until('\n'), word);
                    if(s.match('=')) { // Explicit argument
                        if(s.match('{')) { // Sweep
                            string sweep = s.until('}');
                            assert(!arguments.contains(key));
                            if(sweeps.contains(key)) { // User overrides local sweep
                                assert_(sweeps.at(key));
                                rule.sweeps.insert(String(key), copy(sweeps.at(key)));
                            } else {
                                if(::find(sweep,".."_)) {
                                    TextData s (sweep);
                                    int begin = s.integer(); s.skip(".."_); int end = s.integer(); assert(!s);
                                    assert(begin >= 0 && end > begin);
                                    array<Variant> sequence;
                                    for(uint i: range(begin, end+1)) sequence << i;
                                    assert_(sequence);
                                    rule.sweeps.insert(String(key), copy(sequence));
                                } else rule.sweeps.insert(String(key), apply<Variant>(split(sweep,','), [](const string& o){return String(o);}));
                            }
                        } else {
                            if(s.match('\'')) rule.argumentExps.insert(String(key), Rule::Expression(String(s.until('\''))));
                            else {
                                string word = s.word("-_"_);
                                assert_(word, "Unquoted literal", "'"_+s.slice(0,s.index)+"'"_);
                                parameters += word;
                                rule.argumentExps.insert(String(key), Rule::Expression(String(word), Rule::Expression::Value));
                            }
                        }
                    }
                    else if(resultNames.contains(key)) {
                        rule.inputs << key; // Result input
                        if(s.match("[]"_)) { assert_(outputs.size==1); assert_(rule.inputs.size==1); assert_(!rule.arrayOperation); rule.arrayOperation = true; }
                    } else {
                        if(rule.operation) rule.inputs << key; // Argument value
                        else if(sweeps.contains(key)) rule.sweeps.insert(String(key), copy(sweeps.at(key))); // Sweep value
                        else if(arguments.contains(key)) rule.sweeps.insert(String(key), move(array<Variant>()<<copy(arguments.at(key)))); // Single sweep value
                        else if(pass>=1) error("Undefined argument", key, arguments, sweeps);
                    }
                }
                for(string output: outputs) { assert_(!resultNames.contains(output), "Multiple result definitions for", output); resultNames << output; }
                rule.outputs = move(outputs);
                rules << move(rule);
            }
        }

        arguments.clear(); targetsSweeps.clear(); sweeps.clear(); targets.clear(); results.clear(); array<string> specialArguments;
        for(const string& argument: allArguments) { // Parses generic arguments (may affect process definition)
            TextData s (argument); string key = s.word("-_."_);
            if(s.match('=')) { // Explicit argument
                string scope, parameter = key;
                if(key.contains('.')) scope=section(key, '.', 0, 1), parameter=section(key, '.', 1, 2);
                assert_(parameters.contains(parameter),"Invalid parameter", parameter);
                string value = s.untilEnd();
                assert_(value);
                if(scope) {
                    bool match=false;
                    for(Rule& rule: rules) for(const string& output: rule.outputs) if(startsWith(output, scope)) {
                        match = true;
                        rule.argumentExps[parameter] = Rule::Expression(String(value));
                    }
                    assert_(match, scope);
                }
                else if(arguments.contains(parameter)) { sweeps.insert(String(parameter), move(array<Variant>()<<String(arguments.take(parameter))<<String(value))); }
                else if(sweeps.contains(parameter)) { sweeps.at(parameter) << String(value); assert_(sweeps.at(parameter).size>1); }
                else arguments.insert(String(parameter), String(value));
            }
            else if(resultNames.contains(argument)) targets << argument;
            else if(parameters.contains(argument)) arguments.insert(String(argument), String());
            else specialArguments << argument;
        }
        for(auto arg: defaultArguments) if(!arguments.contains(arg.key)) arguments.insert(copy(arg.key), copy(arg.value));
        for(auto arg: defaultSweeps) { assert_(arg.value); if(!sweeps.contains(arg.key)) sweeps.insert(copy(arg.key), copy(arg.value)); }
        results.grow(targets.size);
        for(uint i: range(targets.size)) {
            array<string> stack; stack << targets[i];
            while(stack) { // Traverse dependency to get all intermediate result names
                string result = stack.pop();
                results[i] += result;
                const Rule& rule = ruleForOutput(result);
                if(&rule) stack << rule.inputs;
            }
        }
        for(uint i: range(targets.size)) {
            Sweeps targetSweeps;
            Dict args = copy(arguments);
            for(auto sweep: sweeps) { // Defines sweep variables
                assert_(sweep.value.size>1, targets[i], sweeps);
                if(args.contains(sweep.key)) {
                    if(!defaultArguments.contains(sweep.key)) assert_(!arguments.contains(sweep.key), "Sweep overrides argument", sweep.key, defaultArguments);
                    args.at(sweep.key) = str(sweep.value,',');
                }
                else args.insert(copy(sweep.key), str(sweep.value,','));
            }
            for(auto sweep: sweeps) { // Discards irrelevant sweeps
                bool relevant = false;
                if(evaluateArguments(targets[i],args).contains(sweep.key)) relevant=true;
                if(relevant) targetSweeps.insert(copy(sweep.key), copy(sweep.value));
            }
            targetsSweeps << move(targetSweeps);
        }
        this->parseSpecialArguments(specialArguments);
    }
    return targets;
}

bool Process::isDefined(const string& parameter) {
    if(arguments.contains(parameter)) return true;
    for(const Sweeps& sweeps: targetsSweeps) if(sweeps.contains(parameter)) return true;
    return false;
}

Rule& Process::ruleForOutput(const string& target) { for(Rule& rule: rules) for(const string& output: rule.outputs) if(output==target) return rule; return *(Rule*)0; }

const Dict& Process::evaluateArguments(const string& target, const Dict& scopeArguments, bool local, bool sweep, const string& scope) {
    for(const Evaluation& e: cache) if(e.target==target && e.input==scopeArguments && e.local==local && e.sweep==sweep) return e.output;
    const Rule& rule = ruleForOutput(target);
    Dict args;
    if(!&rule && scopeArguments.contains(target)) { // Conversion from argument to result
        args.insert(String(target), copy(scopeArguments.at(target)));
        cache << unique<Evaluation>(String(target), copy(scopeArguments), local, sweep, move(args));
        return cache.last()->output;
    }
    assert_(&rule, "No rule generating '"_+target+"'"_, scope, scopeArguments);

    Dict scopeArgumentsAndSweeps = copy(scopeArguments);
    for(auto arg: rule.sweeps) {
        assert_(arg.value, arg.key);
        if(scopeArgumentsAndSweeps.contains(arg.key)) {}
        else scopeArgumentsAndSweeps.insert(copy(arg.key), str(arg.value,',')); // Explicits sweep as arguments (for argument validation)
    }

    // Recursively evaluate to invalid cache on argument changes
    for(const string& input: rule.inputs) {
        for(auto arg: evaluateArguments(input, scopeArgumentsAndSweeps, false, sweep, scope+"/"_+target)) {
            if(args.contains(arg.key)) assert_(args.at(arg.key)==arg.value, "'"_+arg.key+"'"_, "'"_+arg.value+"'"_, args, scope+"/"_+target+"/"_+input);
            else args.insert(copy(arg.key), copy(arg.value));
        }
    }
    for(const string& input: rule.inputs) {
        if(!sweep) { // Removes sweep handled by inputs sweep generator (Prevents duplicate sweep by process sweeper)
            const Rule& rule = ruleForOutput(input);
            if(&rule) for(const string& parameter: rule.sweeps.keys) args.remove(parameter);
        }
    }

    /// Relevant rule parameters (from operation and argument expressions)
    array<string> parameters;
    if(rule.operation) {
        unique<Operation> operation = Interface<Operation>::instance(rule.operation);
        assert_(operation, "Operation", rule.operation, "not found in", Interface<Operation>::factories.keys);
        parameters += split(operation->parameters());
    }
    for(auto arg: rule.argumentExps) if(arg.value.type==Rule::Expression::Value) parameters += arg.value;

    for(auto arg: scopeArgumentsAndSweeps) if(parameters.contains(arg.key)) {
        if(args.contains(arg.key)) args.remove(arg.key); // Removes inherited sweep
        args.insert(copy(arg.key), copy(arg.value)); // Filters relevant scope arguments
    }
    for(auto arg: rule.sweeps) { // Explicits sweep as arguments (for cache validation)
        assert_(arg.value, arg.key);
        if(args.contains(arg.key)) args.remove(arg.key); // Sweep overrides local argument
        args.insert(copy(arg.key), str(arg.value,','));
    }
    for(auto arg: rule.argumentExps) { // Evaluates local arguments
        if(args.contains(arg.key)) continue;
        assert_(parameters.contains(arg.key), "Irrelevant argument", arg.key, "for", rule);
        if(arg.value.type == Rule::Expression::Value) { // Local value argument
            assert_(scopeArgumentsAndSweeps.contains(arg.value), rule, ": Undefined", arg.value);
            args.insert(copy(arg.key), copy(scopeArgumentsAndSweeps.at(arg.value)));
        } else if(local) { // Local literal argument (only needed for Operation execution)
            args.insert(copy(arg.key), copy((const Variant&)arg.value));
        }
    }
    //log(scope+"/"_+target, scopeArgumentsAndSweeps, "->"_, args);
    cache << unique<Evaluation>(target, move(scopeArgumentsAndSweeps), local, sweep, move(args));
    return cache.last()->output;
}

int Process::indexOf(const string& target, const Dict& arguments) {
    const Dict& relevantArguments = Process::evaluateArguments(target, arguments);
    for(uint i: range(results.size)) if(results[i]->name==target && results[i]->relevantArguments==relevantArguments) return i;
    return -1;
}
const shared<Result>& Process::find(const string& target, const Dict& arguments) { int i = indexOf(target, arguments); return i>=0 ? results[i] : *(shared<Result>*)0; }

/// Returns if computing \a target with \a arguments would give the same result now compared to \a queryTime
bool Process::sameSince(const string& target, int64 queryTime, const Dict& arguments) {
    const shared<Result>& result = find(target, arguments);
    if(&result) {
        if(result->timestamp <= queryTime) queryTime = result->timestamp; // Result is still valid if inputs didn't change since it was generated
        else return false; // Result changed since query
    }
    const Rule& rule = ruleForOutput(target);
    if(!&rule && arguments.contains(target)) return true; // Conversion from argument to result
    for(const string& input: rule.inputs) { // Inputs changed since result (or query if result was discarded) was last generated
        if(rule.sweeps) { // Check all sweeped results
            assert_(rule.sweeps.size()==1);
            Dict args = copy(arguments);
            string parameter = rule.sweeps.keys.first(); // Removes first parameter and loop over it
            if(args.contains(parameter)) args.remove(parameter); // Sweep overrides (default) argument
            for(const Variant& value: rule.sweeps.at(parameter)) {
                args.insert(String(parameter), copy(value));
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

shared<Result> Process::getResult(const string& target, const Dict& arguments, const string&) {
    const shared<Result>& result = find(target, arguments);
    if(&result && sameSince(target, result->timestamp, arguments)) { assert(result->data.size); return share(result); }
    error("Anonymous process manager unimplemented"_);
}

array<shared<Result> > Process::execute(const string& target, const Sweeps& sweeps, const Dict& arguments) {
    array<shared<Result>> results;
    if(sweeps) {
        Dict args = copy(arguments);
        for(auto sweep: sweeps) { // Defines all sweeps parameters
            if(args.contains(sweep.key)) args.remove(sweep.key); // Allows sweep to override default arguments
            args.insert(copy(sweep.key), str(sweep.value,','));
        }
        Sweeps remaining = copy(sweeps);
        string parameter = sweeps.keys.first(); // Removes first parameter and loop over it
        array<Variant> sweep = remaining.take(parameter);

        if(!evaluateArguments(target,args,false,false).contains(parameter)) return execute(target, remaining, args);
        for(Variant& value: sweep) {
            args.at(parameter) = move(value);
            array<shared<Result>> result = execute(target, remaining, args);
            results << move(result);
            if(result.size==1) { // Early exits last sweep on null results
                if(!results.last()->data || (results.last()->metadata=="scalar"_ && TextData(results.last()->data).decimal() == 0)) break;
            }
        }
    } else { // Actually generates targets when sweeps have been explicited
        log(">>", target, evaluateArguments(target, arguments, false, false));
        Time time;
        results << getResult(target, arguments);
        if((uint64)time > 100) log("<<", target, time);
    }
    return results;
}

array<array<shared<Result> > > Process::execute(const ref<string>& allArguments, const string& definition) {
    array<array<shared<Result>>> targetResults; // Generated results for each target
    array<string> targets = configure(allArguments, definition);
    for(uint i: range(targets.size)) targetResults << execute(targets[i], targetsSweeps[i], arguments);
    return targetResults;
}

array<string> PersistentProcess::configure(const ref<string>& allArguments, const string& definition) {
    assert_(!results);
    array<string> targets = Process::configure(allArguments, definition);
    if(arguments.contains("storageFolder"_)) storageFolder = Folder(arguments.at("storageFolder"_),currentWorkingDirectory());
    // Maps intermediate results from file system
    for(const String& path: storageFolder.list(Files|Folders)) {
        TextData s (path); string name = s.whileNot('{');
        if(path==name || !&ruleForOutput(name)) { // Removes invalid data
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
                results << shared<ResultFile>(name, file.modifiedTime(), move(arguments), String(metadata), file.read(file.size()), path, storageFolder);
            } else { // Memory-mapped file
                results << shared<ResultFile>(name, file.modifiedTime(), move(arguments), String(metadata), Map(file, Map::Prot(Map::Read|Map::Write)), path, storageFolder);
            }
        } else { // Folder
            Folder folder (path, storageFolder);
            shared<ResultFile> result(name, folder.modifiedTime(), move(arguments), String(metadata), String(), path, storageFolder);
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

shared<Result> PersistentProcess::getResult(const string& target, const Dict& arguments, const string& scope) {
    const Rule& rule = ruleForOutput(target);
    if(!&rule && arguments.contains(target)) // Conversion from argument to result
        return shared<ResultFile>(target, 0, Dict(), String("argument"_), copy(arguments.at(target)), ""_, ""_);
    assert_(&rule, target);

    const shared<Result>& result = find(target, arguments);
    if(&result && sameSince(target, result->timestamp, arguments)) return share(result); // Returns a cached result if still valid
    // Otherwise regenerates target using new inputs, arguments and/or implementations

    array<shared<Result>> inputs;
    if(!rule.sweeps) for(const string& input: rule.inputs) inputs << getResult(input, arguments, scope+"/"_+target);
    for(const shared<Result>& input: inputs) {
        ResultFile& result = *(ResultFile*)input.pointer;
        if(result.fileName) touchFile(result.fileName, result.folder, false); // Updates last access time for correct LRU cache behavior
    }

    if(!rule.operation && !rule.sweeps && !rule.argumentExps) { // Simple forwarding rule
        assert_(inputs.size == 1 && rule.outputs.size==1, "FIXME: Only single inputs can be forwarded");
        return move(inputs.first());
    }

    unique<Operation> operation = rule.operation ? Interface<Operation>::instance(rule.operation) : nullptr;

    array<shared<Result>> outputs;
    for(uint index: range(rule.outputs.size)) {
        const string& output = rule.outputs[index];

        if(&find(output, arguments)) { // Reuses same result file
            shared<ResultFile> result = results.take(indexOf(output, arguments));
            String fileName = move(result->fileName); assert(!result->fileName.size);
            rename(fileName, output, storageFolder);
        }

        Map map;
        int64 outputSize = operation ? operation->outputSize(arguments, cast<Result*>(inputs), index) : 0;
        if(outputSize) { // Creates (or resizes) and maps an output result file
            while(outputSize > (existsFile(output, storageFolder) ? File(output, storageFolder).size() : 0) + freeSpace(storageFolder)) {
                long minimum=realTime(); String oldest;
                for(String& path: storageFolder.list(Files)) { // Discards oldest unused result (across all process hence the need for ResultFile's inter process reference counter)
                    TextData s (path); s.until('}'); int userCount=s.mayInteger(); if(userCount>1 || !s.match('.')) continue; // Used data or not a process data
                    if(File(path, storageFolder).size() < 64*1024) continue; // Keeps small result files
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
        outputs << shared<ResultFile>(output, currentTime(), Dict(), String(), move(map), output, storageFolder);
    }
    assert_(outputs);

    Time time;
    const Dict* relevantArguments;
    if(operation) {
        relevantArguments = &evaluateArguments(target, arguments);
        //log(scope+"/"_+target);
        if(rule.arrayOperation) { // Operation with elements inputs are run on each element
            assert_(inputs.size==1 && inputs[0]->elements && !inputs[0]->data && outputs.size==1 && !outputs[0]->data);
            for(auto e: inputs[0]->elements) {
                Result input(inputs[0]->name, inputs[0]->timestamp, copy(inputs[0]->relevantArguments), copy(inputs[0]->metadata), copy(e.value));
                Result output(outputs[0]->name, 0, {}, {}, {});
                operation->execute(evaluateArguments(target, arguments, true), {&output}, {&input});
                if(!outputs[0]->metadata) outputs[0]->metadata = copy(output.metadata);
                assert_(outputs[0]->metadata == output.metadata);
                assert_(output.data, target, rule.operation);
                outputs[0]->elements.insert(copy(e.key), move(output.data));
            }
        } else {
            operation->execute(evaluateArguments(target, arguments, true), cast<Result*>(outputs), cast<Result*>(inputs));
        }
    } else { // Sweep generator
           assert_(rule.sweeps.size()==1, "FIXME: Only single sweeps can be generated");
           assert_(rule.inputs.size == 1 && outputs.size==1, "FIXME: Only single target sweeps can be generated");

           Dict args = copy(arguments);
           string parameter = rule.sweeps.keys.first(); // Removes first parameter and loop over it
           assert_(rule.sweeps.at(parameter), "Single value sweep", parameter, rule.sweeps.at(parameter));
           if(args.contains(parameter)) args.remove(parameter); // Sweep overrides (default) argument
           args.insert(String(parameter), str(rule.sweeps.at(parameter),','));
           relevantArguments = &evaluateArguments(target, arguments);
           assert_(relevantArguments->contains(parameter), "Irrelevant sweep parameter", parameter, "for", rule.inputs.first(), relevantArguments);

           String metadata, data;
           for(const Variant& value: rule.sweeps.at(parameter)) {
               args.remove(parameter);
               args.insert(String(parameter), copy(value));
               shared<Result> result = getResult(rule.inputs.first(), args, scope+"/"_+target);
               if(result->metadata=="scalar"_ || result->metadata=="argument"_) {
                   if(!metadata) metadata = String("sweep.tsv"_);
                   assert(metadata == "sweep.tsv"_);
                   data << value << "\t"_ << result->data << (endsWith(result->data,"\n"_)?""_:"\n"_);
                   if(TextData(result->data).decimal() == 0) break;
               } else {
                   if(!metadata) metadata = copy(result->metadata);
                   assert(metadata == result->metadata);
                   if(!result->data) break;
                   outputs[0]->elements.insert(copy(value), copy(result->data));
               }
           }
           outputs[0]->metadata = move(metadata);
           outputs[0]->data = move(data);
    }
    /*if((uint64)time>100)*/ log(rule, relevantArguments, time);

    for(shared<Result>& output : outputs) {
        shared<ResultFile> result = move(output);
        result->timestamp = realTime();
        result->relevantArguments = copy(*relevantArguments);
        if(result->elements) { // Copies each elements data from anonymous memory to files in a folder
            assert_(!result->maps && !result->data);
            assert_(result->elements.size() > 1);
            Folder folder(result->fileName, result->folder, true);
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
        results << move(result);
    }
    return share(find(target, arguments));
}

/// Returns the last value of a result with multiple elements
//TODO: builtin index operator
class(Last, Operation) {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        outputs[0]->metadata = copy(inputs[0]->metadata);
        outputs[0]->data = copy(inputs[0]->elements.values.last());
    }
};
