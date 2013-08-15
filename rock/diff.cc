#include "process.h"
#include "volume-operation.h"
#include "sample.h"

/// Sets voxels where A and B values differs
static void diff(Volume24& target, const Volume16& A, const Volume16& B) {
    assert_(A.size()==B.size() && target.size() == A.size());
    for(uint z: range(target.sampleCount.z)) for(uint y: range(target.sampleCount.z)) for(uint x: range(target.sampleCount.z)) {
        uint a=A(x,y,z), b=B(x,y,z); uint8 c=a*0xFF/A.maximum;
        target(x,y,z) = a==b ? bgr{c,c,c} : a<b ? bgr{0xFF,0,0} : bgr{0,0,0xFF};
    }
}
class(Diff, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(bgr); }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        diff(outputs[0], inputs[0], inputs[1]);
    }
};

/// Compares results of enabling a given parameter when generating a given target
class(Compare, Operation) {
    virtual size_t outputSize(const Dict& args unused, const ref<Result*>& inputs unused, uint index unused) { return -1; } // Prevent output allocation (will be done in custom ResultManager::compute call)
    string parameters() const override { return "target parameter A B hold"_; }
    void execute(const Dict& arguments, const Dict& localArguments, const ref<Result*>& outputs, const ref<Result*>&, ResultManager& results) override {
        shared<Result> hold;
        if(arguments.contains("hold"_)) hold = results.getResult(arguments.at("hold"_), arguments); // Prevents last common ancestor from being recycled (FIXME: automatic?)
        string target = arguments.at("target"_), parameter = arguments.at("parameter"_);
        Dict args = copy(arguments);
        args[parameter]=arguments.value("A"_,0);
        shared<Result> A = results.getResult(target, args);
        args[parameter]=arguments.value("B"_,1);
        shared<Result> B = results.getResult(target, args);

        results.compute("Diff"_, {move(A),move(B)}, {outputs[0]->name}, arguments, localArguments, Dict());
    }
};

/// Computes the unconnected and connected pore space volume versus pruning radius and the largest pruning radius keeping both Z faces connected
class(Sweep, Operation) {
    string parameters() const override { return "target sweep hold"_; }
    virtual bool sameSince(const Dict& args unused, int64 queryTime, ResultManager& results unused) {
        string target = args.at("target"_), sweep = args.at("sweep"_);
        for(string argSet: split(sweep,';')) {
            Dict allArgs = copy(args);
            TextData s("{"_+replace(String(argSet),',','|')+"}"_);
            for(auto arg: parseDict(s)) allArgs.insert(copy(arg.key)) = copy(arg.value);
            if(!results.sameSince(target, queryTime, allArgs)) return false;
        }
        return true;
    }
    void execute(const Dict& arguments, const Dict&, const ref<Result*>& outputs, const ref<Result*>&, ResultManager& process) override {
        Dict args = copy(arguments);
        shared<Result> hold;
        if(args.contains("hold"_)) hold = process.getResult(args.take("hold"_), args); // Prevents last common ancestor from being recycled (FIXME: automatic?)
        String target = args.take("target"_), sweep = args.take("sweep"_);
        map<String, shared<Result>> results;
        for(string argSet: split(sweep,';')) {
            Dict allArgs = copy(args);
            TextData s("{"_+replace(String(argSet),',','|')+"}"_);
            for(auto arg: parseDict(s)) allArgs.insert(copy(arg.key)) = copy(arg.value);
            log(target, allArgs);
            shared<Result> result = process.getResult(target, allArgs);
            if(result->metadata=="scalar"_) log(result->name, result->data);
            results.insert(String(argSet), result);
        }
        outputElements(outputs, "sweep"_, results.values[0]->metadata, [&]{
            map<String, buffer<byte>> elements;
            for(auto result: results) elements.insert(copy(result.key), copy(result.value->data));
            return elements;
        });
    }
};

/// Concatenates scalar elements in a single scalar map result
class(Concatenate, Operation), virtual Pass {
    void execute(const Dict&, Result& output, const Result& source) override {
        ScalarMap map;
        for(auto element: source.elements) map.insert(copy(element.key), parseScalar(element.value));
        output.metadata = String("tsv"_);
        output.data = toASCII(map);
    }
};
