#pragma once
#include "array.h"

/// Concatenates a \a cat with a cat
template<class A, class B, class T> struct cat {
    A a; B b;
    cat(A&& a, B&& b) : a(move(a)), b(move(b)) {}
    int size() const { return a.size() + b.size(); }
    void copy(array<T>& target) const { a.copy(target); b.copy(target); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
};
template<class T, class A, class B, class C, class D>
cat<cat<A, B, T>, cat<C, D, T>, T> operator+(cat<A, B, T>&& a, cat<C, D, T>&& b) { return {move(a),move(b)}; }

/// Concatenates a \a cat with a value
template<class A, class T> struct cat<A, T, T> {
    A a; const T b;
    cat(A&& a, const T b) : a(move(a)), b(b) {}
    int size() const { return a.size() + 1; };
    void copy(array<T>& target) const { a.copy(target); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
};
template<class T, class A, class B> cat<cat<A, B, T>, T, T> operator+(cat<A, B, T>&& a, T b) { return {move(a),b}; }

/// Concatenates a \a cat with a ref
template<class A, class T> struct cat<A, ref<T>, T> {
    A a; const ref<T> b;
    cat(A&& a, ref<T> b) : a(move(a)), b(b) {}
    int size() const { return a.size() + b.size; };
    void copy(array<T>& target) const { a.copy(target); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
};
template<class T, class A, class B> cat<cat<A, B, T>, ref<T>, T> operator+(cat<A, B, T>&& a, ref<T> b) { return {move(a),b}; }
// Required for implicit string literal conversion
template<class T, class A, class B, size_t N> cat<cat<A, B, T>, ref<T>, T> operator+(cat<A, B, T>&& a, const T(&b)[N]) { return {move(a),b}; }

/// Concatenates a \a cat with an array
template<class A, class T> struct cat<A, array<T>, T> {
    A a; array<T> b;
    cat(A&& a, array<T>&& b) : a(move(a)), b(move(b)) {}
    int size() const { return a.size() + b.size; };
    void copy(array<T>& target) const { a.copy(target); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
};
template<class T, class A, class B>
cat<cat<A, B, T>, array<T>, T> operator+(cat<A, B, T>&& a, array<T>&& b) { return {move(a),move(b)}; }

/// Concatenate a value with a ref
generic struct cat<T, ref<T>, T> {
    T a; ref<T> b;
    cat(T a, ref<T> b) : a(a), b(b) {}
    int size() const { return 1 + b.size; };
    void copy(array<T>& target) const { target.append(a); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
};
generic cat<T, ref<T>, T> operator+(T a, ref<T> b) { return {a,b}; }

/// Concatenates a ref with a value
generic struct cat<ref<T>, T, T> {
    ref<T> a; T b;
    cat(ref<T> a, T b) : a(a), b(b) {}
    int size() const { return a.size + 1; };
    void copy(array<T>& target) const { target.append(a); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
};
generic cat<ref<T>, T, T> operator+(ref<T> a, T b) { return {a,b}; }

/// Concatenates a ref with a ref
generic struct cat<ref<T>, ref<T>, T> {
    ref<T> a; ref<T> b;
    cat(ref<T> a, ref<T> b) : a(a), b(b) {}
    int size() const { return a.size + b.size; };
    void copy(array<T>& target) const { target.append(a); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
};
generic cat<ref<T>,ref<T>, T> operator+(ref<T> a, ref<T> b) { return {a,b}; }
template<class T, size_t N> cat<ref<T>,ref<T>, T> operator+(const T(&a)[N], ref<T> b) { return {a,b}; }
template<class T, size_t N> cat<ref<T>,ref<T>, T> operator+(ref<T> a, const T(&b)[N]) { return {a,b}; }

/// Concatenates a ref with an array
generic struct cat<ref<T>, array<T>, T> {
    ref<T> a; array<T> b;
    cat(ref<T> a, array<T>&& b) : a(a), b(move(b)) {}
    int size() const { return a.size + b.size; };
    void copy(array<T>& target) const { target.append(a); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
};
generic cat<ref<T>,array<T>, T> operator+(ref<T> a, array<T>&& b) { return {a,move(b)}; }
template<class T, size_t N> cat<ref<T>,array<T>, T> operator+(const T(&a)[N], array<T>&& b) { return {a,move(b)}; }

/// Concatenates a ref with a cat
template<class B, class T> struct cat<ref<T>, B, T> {
    const ref<T> a; B b;
    cat(ref<T> a, B&& b) : a(a), b(move(b)) {}
    int size() const { return a.size + b.size(); };
    void copy(array<T>& target) const { target.append(a); b.copy(target); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
    //operator ref<T>() const { return array<T>(); }
};
template<class T, class A, class B> cat<ref<T>, cat<A, B, T>, T> operator+(ref<T> a, cat<A, B, T>&& b) { return {a,move(b)}; }

/// Concatenates an array with a value
generic struct cat<array<T>, T, T> {
    array<T> a; T b;
    cat(array<T>&& a, T b) : a(move(a)), b(b) {}
    int size() const { return a.size + 1; };
    void copy(array<T>& target) const { target.append(a); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
};
generic cat<array<T>, T, T> operator+(array<T>&& a, T b) { return {move(a),b}; }

/// Concatenates an array with a ref
generic struct cat<array<T>, ref<T>, T> {
    array<T> a; ref<T> b;
    cat(array<T>&& a, ref<T> b) : a(move(a)), b(b) {}
    int size() const { return a.size + b.size; };
    void copy(array<T>& target) const { target.append(a); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
};
generic cat<array<T>,ref<T>, T> operator+(array<T>&& a, ref<T> b) { return {move(a),b}; }
// Required for implicit string literal conversion
template<class T, size_t N> cat<array<T>,ref<T>, T> operator+(array<T>&& a, const T(&b)[N]) { return {move(a),b}; }

/// Concatenates an array with an array
generic struct cat<array<T>, array<T>, T> {
    array<T> a; array<T> b;
    cat(array<T>&& a, array<T>&& b) : a(move(a)), b(move(b)) {}
    int size() const { return a.size + b.size; };
    void copy(array<T>& target) const { target.append(a); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
};
generic cat<array<T>,array<T>, T> operator+(array<T>&& a, array<T>&& b) { return {move(a),move(b)}; }
