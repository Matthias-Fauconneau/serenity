#include "process.h"
#include "volume-operation.h"
#include "sample.h"

/// Sets voxels where A and B values differs
static void diff(Volume24& target, const Volume16& A, const Volume16& B) {
    assert_(A.size()==B.size() && target.size() == A.size());
    for(uint z: range(target.sampleCount.z)) for(uint y: range(target.sampleCount.z)) for(uint x: range(target.sampleCount.z)) {
        uint a=A(x,y,z), b=B(x,y,z); uint8 c=a*0xFF/A.maximum;
        //target(x,y,z) = a==b ? bgr{c,c,c} : a<b ? bgr{0xFF,0,0} : bgr{0,0,0xFF};
        target(x,y,z) = a==b ? bgr{c,0,0} : a<b ? bgr{0,0xFF,0} : bgr{c,c,c};
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

        /*Dict relevantArguments = copy(results.relevantArguments(target, arguments));
        relevantArguments.insert(String("target"_), String(target));
        relevantArguments.insert(String("parameter"_), String(parameter));
        String name = move(outputs[0]->name); // Removes already stored output from this tool's results (FIXME)
        Result* output = 0;
        for(const shared<Result>& result: results.results) if(result->name==name && result->relevantArguments==relevantArguments) { output=result.pointer; break; }
        if(!output) {
            results.compute("Diff"_, {move(A),move(B)}, {name}, arguments, relevantArguments, Dict());
            for(const shared<Result>& result: results.results) if(result->name==name && result->relevantArguments==relevantArguments) { output=result.pointer; break; }
        }
        assert_(output);
        output->relevantArguments = results.localArguments(name, arguments);*/
        results.compute("Diff"_, {move(A),move(B)}, {outputs[0]->name}, arguments, localArguments, Dict());
    }
};

