#include "process.h"
#include "volume-operation.h"
#include "sample.h"

/// Sets voxels where A and B values differs
static void diff(Volume24& target, const Volume16& A, const Volume16& B) {
    assert_(A.size()==B.size() && target.size() == A.size());
    for(uint z: range(target.sampleCount.z)) for(uint y: range(target.sampleCount.z)) for(uint x: range(target.sampleCount.z)) {
        uint a=A(x,y,z), b=B(x,y,z); uint8 c=a*0xFF/A.maximum;
        target(x,y,z) = a==b ? bgr{c,c,c} : a<b ? bgr{0,0xFF,0} : bgr{0,0,0xFF};
    }
}
class(Diff, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(bgr); }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        diff(outputs[0], inputs[0], inputs[1]);
    }
};

/// Compares results of enabling a given parameter when generating a given target
class(Compare, Tool) {
    string parameters() const override { return "target parameter hold"_; }
    void execute(const Dict& arguments, const ref<Result*>& outputs, const ref<Result*>&, Process& process) override {
        shared<Result> hold;
        if(arguments.contains("hold"_)) hold = process.getResult(arguments.at("hold"_), arguments); // Prevents last common ancestor from being recycled (FIXME: automatic?)
        string target = arguments.at("target"_), parameter = arguments.at("parameter"_);
        Dict args = copy(arguments);
        args[parameter]=0;
        shared<Result> A = process.getResult(target, args);
        args[parameter]=1;
        shared<Result> B = process.getResult(target, args);

        Dict relevantArguments = copy(process.relevantArguments(target, arguments));
        relevantArguments.insert(String("target"_), String(target));
        relevantArguments.insert(String("parameter"_), String(parameter));
        String name = move(outputs[0]->name); // Removes already stored output from this tool's results
        Result* output = 0;
        for(const shared<Result>& result: process.results) if(result->name==name && result->relevantArguments==relevantArguments) { output=result.pointer; break; }
        if(!output) {
            process.compute("Diff"_, {move(A),move(B)}, {name}, arguments, relevantArguments, Dict());
            for(const shared<Result>& result: process.results) if(result->name==name && result->relevantArguments==relevantArguments) { output=result.pointer; break; }
        }
        assert_(output);
        output->relevantArguments = process.localArguments(name, arguments);
    }
};

