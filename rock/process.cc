#include "process.h"
#include "data.h"
#include "time.h"
#include "math.h"
#include <sys/stat.h>

uint ResultFile::indirectID = 0;

/// Relevant operation parameters
array<string> Rule::parameters() const {
    array<string> parameters;
    if(condition.parameter) parameters << condition.parameter;
    if(operation) parameters += split(Interface<Operation>::instance(this->operation)->parameters());
    return parameters;
}

array<string> Process::configure(const ref<string>& allArguments, const string& definition) {
    array<string> targets;
    Dict defaultArguments; // Process-specified default arguments
    array<string> parameters; /// All valid parameters accepted by defined rules (used by conditions or operations)

    for(TextData s(definition); s;) { //FIXME: use parser generator
        s.skip();
        if(s.match('#')) { s.until('\n'); continue; }
        Rule rule;
        if(s.match("if"_)) {
            s.whileAny(" \t"_);
            string op;
            string value;
            if(s.match('!')) op="=="_, value="0"_;
            string parameter = s.word("_-."_);
            parameters += parameter;
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
            if(!s.match('\'')) error("Unquoted literal", key, s.whileNo(" \t\n"_));
            string value = s.until('\''); // Literal
            assert_(!defaultArguments.contains(key),"Multiple default argument definitions for",key);
            defaultArguments.insert(String(key), String(value));
        } else {
            string word = s.identifier("_-"_);
            assert_(word, "Expected operator or input for", outputs);
            if(!Interface<Operation>::factories.contains(word)) rule.inputs << word; // Forwarding rule
            else rule.operation = word; // Generating rule
            s.whileAny(" \t"_);
            for(;!s.match('\n'); s.whileAny(" \t"_)) {
                if(s.match('#')) { s.whileNot('\n'); continue; }
                string key = s.identifier("_-."_); s.whileAny(" \t"_);
                assert_(key, s.until('\n'), word);
                if(s.match('=')) { // Local argument
                    s.whileAny(" \t"_);
                    rule.arguments.insert(String(key), String(s.whileNo(" \t\n"_)));
                }
                else if(resultNames.contains(key)) rule.inputs << key; // Result input
                else if(rule.operation) rule.inputs << key; // Argument value
            }
            resultNames += outputs; //for(string output: outputs) { assert_(!resultNames.contains(output), "Multiple result definitions for", output); resultNames << output; }
            rule.outputs = move(outputs);
            parameters += rule.parameters();
            if(!rule.operation) assert_(!rule.arguments && rule.inputs.size == 1 && rule.outputs.size==1, "Invalid forwarding rule",rule,"or \nNo such operator", word, "in", Interface<Operation>::factories.keys);
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
            if(specialParameters.contains(key)) this->specialArguments.insert(String(key), String(value));
            else arguments.insert(String(key), String(value));
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
        assert_(parameters.contains(arg.key),"Irrelevant local argument",arg.key,"for",rule.operation,"generating",target);
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
    if(rule.operation && parse(Interface<Operation>::version(rule.operation))*1000000000l > queryTime) return false; // Implementation changed since query
    return !rule.operation || Interface<Operation>::instance(rule.operation)->sameSince(arguments, queryTime, *this); // Let high level operations check its custom input for correct cache behavior
}

array<string> PersistentProcess::configure(const ref<string>& allArguments, const string& definition) {
    assert_(!results);
    array<string> targets = Process::configure(allArguments, definition);
    if(specialArguments.contains("storageFolder"_)) {
        log("Storing data in user-specified folder:",specialArguments.at("storageFolder"_));
        storageFolder = Folder(specialArguments.at("storageFolder"_),currentWorkingDirectory());
    }
    if(specialArguments.contains("indirect"_)) {
        log("Storing metadata in auxiliary files (instead of file names)");
        ResultFile::indirectID=1;
        for(const String& file: storageFolder.list(Files|Folders|Sorted)) { // Starts from the highest ID to avoid collisions with previous runs
            TextData s(file); ResultFile::indirectID=max(ResultFile::indirectID,s.mayInteger()+1);
        }
    }
    for(const String& file: storageFolder.list(Files|Folders|Sorted)) {
        String id; String dataFile;
        if(ResultFile::indirectID) {
            if(!endsWith(file,".data"_) && !endsWith(file,".meta"_)) { removeFileOrFolder(file, storageFolder); continue; }
            string fileID = file.slice(0,file.size-".data"_.size);
            dataFile=fileID+".data"_;
            if(!existsFile(fileID+".meta"_, storageFolder)) { warn("Missing metadata", fileID); removeFileOrFolder(dataFile,storageFolder); continue; }
            if(!existsFile(fileID+".data"_, storageFolder)) { removeFileOrFolder(file,storageFolder); continue; }
            if(!endsWith(file,"meta"_)) continue; // Skips load when listing data file to load once listing the metadata file
            id=readFile(fileID+".meta"_, storageFolder);
        } else {
            id=copy(file);
            dataFile=copy(file);
        }
        TextData s (id); string name = s.whileNot('{');
        if(id==name || !&ruleForOutput(name, arguments)) { removeFileOrFolder(dataFile,storageFolder); continue; } // Removes invalid data
        Dict arguments = parseDict(s); s.mayInteger(); s.skip("."_); string metadata = s.untilEnd();
        shared<ResultFile> result;
        if(!existsFolder(dataFile, storageFolder)) {
            File file = File(dataFile, storageFolder, ReadWrite);
            if(file.size()<(1<<16)) { // Small file (<64K)
                result = shared<ResultFile>(name, file.modifiedTime(), move(arguments), String(metadata), file.read(file.size()), dataFile, storageFolder.name());
            } else { // Memory-mapped file
                result = shared<ResultFile>(name, file.modifiedTime(), move(arguments), String(metadata), Map(file, Map::Prot(Map::Read|Map::Write)), dataFile, storageFolder.name());
            }
        } else { // Folder
            Folder folder (dataFile, storageFolder);
            result = shared<ResultFile>(name, folder.modifiedTime(), move(arguments), String(metadata), String(), dataFile, storageFolder.name());
            for(const String& dataFile: folder.list(Files|Sorted)) {
                string key = section(dataFile,'.',0,1), metadata=section(dataFile,'.',1,-1);
                assert_(metadata == result->metadata);
                File file = File(dataFile, folder, ReadWrite);
                if(file.size()<(1<<16)) { // Small file (<64K)
                    result->elements.insert(String(key), file.read(file.size()));
                } else { // Memory-mapped file
                    result->maps << Map(file, Map::Prot(Map::Read|Map::Write));
                    result->elements.insert(String(key), buffer<byte>(result->maps.last()));
                }
            }
        }
        if(ResultFile::indirectID && file!=str(result->fileID)+".meta"_) { // Remap IDs for this run
            rename(file, str(result->fileID)+".meta"_, storageFolder);
            rename(dataFile, str(result->fileID)+".data"_, storageFolder);
        }
        assert_(existsFile(result->dataFile(),storageFolder));
        results << move(result);
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
    if(!&rule && arguments.contains(target)) { // Conversion from argument to result
        return shared<ResultFile>(target, 0, Dict(), String("argument"_), copy(arguments.at(target)), ""_, ""_);
    }
    assert_(&rule, "Unknown rule", target);

    // Simple forwarding rule
    if(!rule.operation) {
        assert_(!rule.arguments && rule.inputs.size == 1 && rule.outputs.size==1, "Invalid forwarding rule",rule,"or \nNo such operator", rule.operation, "in", Interface<Operation>::factories.keys);
        return getResult(rule.inputs.first(), arguments);
    }

    const shared<Result>& result = find(target, arguments);
    if(&result && sameSince(target, result->timestamp, arguments)) {
        return share(result); // Returns a cached result if still valid
    }
    // Otherwise regenerates target using new inputs, arguments and/or implementations

    array<shared<Result>> inputs;
    for(const string& input: rule.inputs) inputs << getResult(input, arguments);

    Dict relevantArguments = Process::localArguments(target, arguments);
    Dict localArguments; array<string> parameters = rule.parameters();
    for(auto arg: relevantArguments) if(parameters.contains(arg.key) && rule.condition.parameter!=arg.key) localArguments.insert(copy(arg.key), copy(arg.value)); // Filters locally relevant arguments

    compute(rule.operation, inputs, rule.outputs, arguments, relevantArguments, localArguments);
    assert_(&find(target, arguments), target, arguments);
    return share(find(target, arguments));
}

void PersistentProcess::compute(const string& operationName, const ref<shared<Result>>& inputs, const ref<string>& outputNames,
                                const Dict& arguments, const Dict& relevantArguments, const Dict& localArguments) {
    unique<Operation> operation = Interface<Operation>::instance(operationName);

    // Updates inputs last access time for correct LRU cache behavior
    for(const shared<Result>& input: inputs) {
        ResultFile& result = *(ResultFile*)input.pointer;
        if(result.id) {
            assert_(existsFile(result.dataFile(),storageFolder), result.dataFile());
            touchFile(result.dataFile(), result.folder, false);
        }
    }

    // Allocates outputs
    array<shared<Result>> outputs;
    for(uint index: range(outputNames.size)) {
        const string& output = outputNames[index];
        int64 outputSize = operation->outputSize(localArguments, cast<Result*>(inputs), index);
        if(outputSize==-1) { // Prevents process from allocating any output Result (used for Operation using custom rules (own compute call), use outputs to pass output names
            outputs << shared<ResultFile>(output, 0, Dict(), String(), String(), ""_, ""_);
            continue;
        }

        String outputFileID = ResultFile::indirectID ? str(ResultFile::indirectID)+".data"_ : String(output); //FIXME: should be transparent

        if(&find(output, arguments)) { // Reuses same result file
            shared<ResultFile> result = results.take(indexOf(output, arguments));
            result->rename(output);
            result->id.clear(); // Reference count update would rewrite metadata
        }

        Map map; uint flags=Map::Populate;
        if(outputSize>0) { // Creates (or resizes) and maps an output result file
            while(outputSize >= (existsFile(outputFileID, storageFolder) && File(outputFileID, storageFolder).size()<capacity(storageFolder) ? File(outputFileID, storageFolder).size() : 0) + available(storageFolder)) {
                long minimum=realTime(); String oldestMeta, oldestData;
                for(String& file: storageFolder.list(Files)) { // Discards oldest unused result (across all process hence the need for ResultFile's inter process reference counter)
                    if(ResultFile::indirectID && !endsWith(file,".meta"_)) continue;
                    string fileID = file.slice(0,file.size-".meta"_.size);
                    String dataFile = ResultFile::indirectID ? fileID+".data"_ : copy(file);
                    if(ResultFile::indirectID && !existsFile(dataFile,storageFolder)) { remove(file,storageFolder); continue; } // Data already removed (removes metadata file)
                    String id = ResultFile::indirectID ? readFile(file, storageFolder) : copy(file);
                    TextData s(id); string name = s.whileNot('{'); if(!s) continue; Dict relevantArguments = parseDict(s); int userCount=s.mayInteger(); if(!s.match('.')) continue; // Used data or not a process data
                    //FIXME: getting process (internal) userCount (BUG: filesystem (global) userCount is not up to date)
                    for(uint i: range(results.size)) if(results[i]->name==name && results[i]->relevantArguments==relevantArguments) { userCount = results[i]->userCount; break; }
                    if(userCount>1) continue; // Used data

                    if(File(dataFile, storageFolder).size() < 2<<20) continue; // Small files won't release much capacity
                    if(!(File(dataFile, storageFolder).stat().st_mode&S_IWUSR)) continue; // Locked file
                    long timestamp = File(dataFile, storageFolder).accessTime();
                    if(timestamp < minimum) minimum=timestamp, oldestMeta=move(id), oldestData=move(dataFile);
                }
                if(!oldestMeta) {
                    if(outputSize<min<int64>(16l<<30,capacity(storageFolder))) {
                        log("Results:");
                        for(const shared<Result>& result: results) {
                            if(result->data.size >= 2<<20 ) log(result->name, result->userCount,  result->data.size);
                        }
                        log("File system:");
                        for(String& file: storageFolder.list(Files)) { // Lists required results
                            if(ResultFile::indirectID && !endsWith(file,".meta"_)) continue;
                            string fileID = file.slice(0,file.size-".meta"_.size);
                            String dataFile = ResultFile::indirectID ? fileID+".data"_ : copy(file);
                            assert(!(ResultFile::indirectID && !existsFile(dataFile,storageFolder)));
                            String id = ResultFile::indirectID ? readFile(file, storageFolder) : copy(file);
                            TextData s(id); s.whileNot('{'); if(!s) continue; parseDict(s); int userCount=s.mayInteger();
                            if(File(dataFile, storageFolder).size() >= 2<<20) log(id, userCount, File(dataFile, storageFolder).size() );
                        }
                        error("Not enough space available for",output," need"_,outputSize/1e6,"MB, only",available(storageFolder)/1e6,"MB available on",storageFolder.name());
                    } else { //Overcommit virtual memory
                        flags = 0;
                        log("Overcommitting", outputSize/1024.0/1024.0/1024.0, "GiB /", capacity(storageFolder)/1024.0/1024.0/1024.0,"GiB"); break; }
                }
                TextData s (oldestMeta); string name = s.whileNot('{'); Dict relevantArguments = parseDict(s);
                for(uint i: range(results.size)) if(results[i]->name==name && results[i]->relevantArguments==relevantArguments) {
                    ((shared<ResultFile>)results.take(i))->id.clear(); // Prevents rename
                    break;
                }
                if(!existsFile(oldestData, storageFolder) || File(oldestData,storageFolder).size()>capacity(storageFolder) || outputSize > File(oldestData,storageFolder).size() + available(storageFolder)) { // Removes if not an allocated file or need to recycle more than one file
                    if(existsFile(oldestData, storageFolder)) {
                        //log("Removing file", oldestData);
                        ::remove(oldestData, storageFolder);
                    } else { // Array output (folder)
                        //log("Removing folder", oldestData);
                        Folder folder(oldestData, storageFolder);
                        for(const String& file: folder.list(Files)) ::remove(file, folder);
                        remove(folder);
                    }
                    continue;
                }
                //log("Recycling file", oldestData);
                ::rename(storageFolder, oldestData, storageFolder, outputFileID); // Renames last discarded file instead of removing (avoids page zeroing)
                break;
            }

            File file(outputFileID, storageFolder, Flags(ReadWrite|Create));
            file.resize(outputSize);
            if(outputSize>=(1<<16)) map = Map(file, Map::Prot(Map::Read|Map::Write), Map::Flags(Map::Shared|flags));
        }
        outputs << shared<ResultFile>(output, currentTime(), Dict(), String(), move(map), output, storageFolder.name());
    }
    assert_(outputs);

    // Executes operation
    Time time;
    Dict args = copy(arguments);
    for(auto arg: relevantArguments) if(args.contains(arg.key)) args.at(arg.key)=copy(arg.value); else args.insert(copy(arg.key), copy(arg.value));
    operation->execute(args, localArguments, cast<Result*>(outputs), cast<Result*>(inputs), *this);
    if((uint64)time>=950) log(operationName, localArguments ? str(localArguments) : ""_, time);

    for(shared<Result>& output : outputs) {
        if(!output->timestamp) continue; // outputSize==-1: output was computed indirectly using an explicit ResultManager::compute
        shared<ResultFile> result = move(output);
        result->timestamp = realTime();
        result->relevantArguments = copy(relevantArguments);
        if(result->elements) { // Copies each elements data from anonymous memory to files in a folder
            assert_(!result->maps && !result->data);
            assert_(result->elements.size() > 1);
            Folder folder(result->dataFile(), result->folder, true);
            for(string file: folder.list(Files)) remove(file,folder);
            for(const_pair<String,buffer<byte>> element: (const map<String,buffer<byte>>&)result->elements) writeFile(element.key+"."_+result->metadata, element.value, folder);
            touchFile(result->dataFile(), result->folder, true);
        } else { // Synchronizes file mappings with results
            size_t mappedSize = 0;
            if(result->maps) {
                assert_(result->maps.size == 1);
                mappedSize = result->maps[0].size;
                assert_(mappedSize);
                if(mappedSize<(1<<16)) result->data = copy(result->data); // Copies to anonymous memory before unmapping
                result->maps.clear();
            }
            File file = 0;
            if(mappedSize && result->data.size <= mappedSize) { // Truncates file to result size
                file = File(result->dataFile(), result->folder, ReadWrite);
                if(result->data.size < mappedSize) file.resize(result->data.size);
                //else only open file to map read-only
            } else { // Copies data from anonymous memory to file
                file = File(result->dataFile(), result->folder, Flags(ReadWrite|Truncate|Create));
                assert(result->data);
                file.write(result->data);
            }
            if(result->data.size>=(1<<16)) { // Remaps file read-only (will be remapped Read|Write whenever used as output again)
                result->maps << Map(file);
                result->data = buffer<byte>(result->maps.last());
            }
        }
        result->rename();
        assert_(existsFile(result->dataFile(),storageFolder), result->dataFile());
        results << move(result);
    }
}
