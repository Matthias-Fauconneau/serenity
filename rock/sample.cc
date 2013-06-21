/// \file kernel-density-estimation.cc Estimates probability density functions and wraps some Sample methods as Operation
#include "sample.h"
#include "operation.h"

class(Sum, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = String("scalar"_);
        target.data = toASCII( parseNonUniformSample<double,double>(source.data).sum() );
    }
};

template<Type X, Type Y> NonUniformSample<X,Y> squareRootVariable(NonUniformSample<X,Y>&& A) { for(X& x: A.keys) x=sqrt(x); return move(A); }
/// Square roots the variable of a distribution
class(SquareRootVariable, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        assert_(endsWith(source.metadata,".tsv"_), "Expected a distribution, not a", source.metadata, source.name, target.name);
        target.metadata = copy(source.metadata);
        target.data = toASCII(squareRootVariable(parseNonUniformSample<double,double>(source.data)));
    }
};

/// Scales variable
template<Type X, Type Y> NonUniformSample<X,Y> scaleVariable(float scalar, NonUniformSample<X,Y>&& A) { for(X& x: A.keys) x *= scalar; return move(A); }
/// Scales the variable of a distribution
class(ScaleVariable, Operation) {
    void execute(const Dict&, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        assert_(endsWith(inputs[0]->metadata,".tsv"_), "Expected a distribution, not a", "'"_+inputs[0]->metadata+"'"_, "for input", "'"_+inputs[0]->name+"'"_);
        assert_(inputs[1]->metadata=="scalar"_ || inputs[1]->metadata=="argument"_, "Expected a scalar or an argument, not a", inputs[1]->metadata,"for", inputs[1]->name);
        outputs[0]->metadata = copy(inputs[0]->metadata);
        outputs[0]->data = toASCII(scaleVariable(TextData(inputs[1]->data).decimal(), parseNonUniformSample<double,double>(inputs[0]->data)));
    }
};

/// Divides two scalars / vectors / sample
class(Div, Operation) {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        outputs[0]->metadata = copy(inputs[0]->metadata);
        if(inputs[0]->metadata=="scalar"_) outputs[0]->data = toASCII(TextData(inputs[0]->data).decimal()/TextData(inputs[1]->data).decimal());
        else if(inputs[0]->metadata=="vector"_) outputs[0]->data = toASCII( (1./TextData(inputs[1]->data).decimal()) * parseVector<double>(inputs[0]->data) );
        else if(endsWith(inputs[0]->metadata,".tsv"_)) outputs[0]->data = toASCII( (1./TextData(inputs[1]->data).decimal()) * parseNonUniformSample<double,double>(inputs[0]->data) );
        else error(inputs[0]->metadata);
    }
};

/// Rounds vectors
class(Round, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = copy(source.metadata);
        if(source.metadata=="vector"_) target.data = toASCII( round( parseVector<double>(source.data) ) );
        else error(source.metadata);
    }
};

/// Returns maximum value
class(Maximum, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = String("scalar"_);
        if(endsWith(source.metadata,".tsv"_)) target.data = toASCII( max( parseMap(source.data).values ) );
        else error(source.metadata);
    }
};

/*/// Computes absolute difference
class(AbsoluteDifference, Operation) {
    void execute(const Dict&, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        assert_(inputs[0]->metadata == inputs[1]->metadata);
        outputs[0]->metadata = copy(inputs[0]->metadata);
        auto A = parseMap(inputs[0]->data), B = parseMap(inputs[1]->data);
        assert_(A.keys == B.keys);
        outputs[0]->data = toASCII( ScalarMap(A.keys, abs( A.values - B.values ) ) );
    }
};*/
