#include "operator.h"

static void average(mref<float> Y, ref<float> R, ref<float> G, ref<float> B) {
    Y.apply([&](float r, float g, float b) {  return (r+g+b)/3; }, R, G, B);
}
void Intensity::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1, const ImageF& X2) const {
	::average(Y, X0, X1, X2);
}

template<Type A, Type T, Type F, Type... Ss> T sum(ref<T> values, F apply, A initialValue, ref<Ss>... sources) {
 return ::reduce(values, [&](A a, T v, Ss... s) { return a+apply(v, s...); }, initialValue, sources...);
}
inline double energy(ref<float> values, float offset=0) { return sum(values, [offset](float value) { return sq(value-offset); }, 0.); }

void Normalize::apply(const ImageF& Y, const ImageF& X) const {
    float mean = ::mean(X);
    double energy = ::energy(X, mean);
    float deviation = sqrt(energy / X.ref::size);
    Y.apply([deviation, mean](const float value) { return (value-mean)/deviation; }, X);
}

inline void mul(mref<float> Y, ref<float> A, ref<float> B) { Y.apply([](float a, float b) { return a*b; }, A, B); }

void Multiply::apply(const ImageF& Y, const ImageF& X0, const ImageF& X1) const { mul(Y, X0, X1); }
