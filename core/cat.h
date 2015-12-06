#pragma once
#include "memory.h"

/// Concatenates a \a cat with a cat
template<Type A, Type B, Type T> struct cat {
    A a; B b;
    cat(A&& a, B&& b) : a(move(a)), b(move(b)) {}
    int size() const { return a.size() + b.size(); }
    void copy(buffer<T>& target) const { a.copy(target); b.copy(target); }
    operator buffer<T>() const { buffer<T> target (size(), 0); copy(target); return move(target); }
};
template<Type T, Type A, Type B, Type C, Type D>
cat<cat<A, B, T>, cat<C, D, T>, T> operator+(cat<A, B, T>&& a, cat<C, D, T>&& b) { return {move(a),move(b)}; }

/// Concatenates a \a cat with a value
template<Type A, Type T> struct cat<A, T, T> {
    A a; const T b;
    cat(A&& a, const T b) : a(move(a)), b(b) {}
    int size() const { return a.size() + 1; }
    void copy(buffer<T>& target) const { a.copy(target); target.append(b); }
    operator buffer<T>() const { buffer<T> target (size(), 0); copy(target); return move(target); }
};
template<Type T, Type A, Type B> cat<cat<A, B, T>, T, T> operator+(cat<A, B, T>&& a, T b) { return {move(a),b}; }

/// Concatenates a \a cat with a ref
template<Type A, Type T> struct cat<A, ref<T>, T> {
    A a; const ref<T> b;
    cat(A&& a, ref<T> b) : a(move(a)), b(b) {}
    int size() const { return a.size() + b.size; }
    void copy(buffer<T>& target) const { a.copy(target); target.append(b); }
    operator buffer<T>() const { buffer<T> target (size(), 0); copy(target); return move(target); }
};
template<Type T, Type A, Type B> cat<cat<A, B, T>, ref<T>, T> operator+(cat<A, B, T>&& a, ref<T> b) { return {move(a),b}; }
// Required for implicit string literal conversion
template<Type T, Type A, Type B, size_t N> cat<cat<A, B, T>, ref<T>, T> operator+(cat<A, B, T>&& a, const T(&b)[N]) { return {move(a),b}; }

/// Concatenates a \a cat with a buffer
template<Type A, Type T> struct cat<A, buffer<T>, T> {
    A a; buffer<T> b;
    cat(A&& a, buffer<T>&& b) : a(move(a)), b(move(b)) {}
    int size() const { return a.size() + b.size; }
    void copy(buffer<T>& target) const { a.copy(target); target.append(b); }
    operator buffer<T>() const { buffer<T> target (size(), 0); copy(target); return move(target); }
};
template<Type T, Type A, Type B>
cat<cat<A, B, T>, buffer<T>, T> operator+(cat<A, B, T>&& a, buffer<T>&& b) { return {move(a),move(b)}; }

/// Concatenate a value with a ref
generic struct cat<T, ref<T>, T> {
    T a; ref<T> b;
    cat(T a, ref<T> b) : a(a), b(b) {}
    int size() const { return 1 + b.size; }
    void copy(buffer<T>& target) const { target.append(a); target.append(b); }
    operator buffer<T>() const { buffer<T> target (size(), 0); copy(target); return move(target); }
};
generic cat<T, ref<T>, T> operator+(T a, ref<T> b) { return {a,b}; }

/// Concatenates a ref with a value
generic struct cat<ref<T>, T, T> {
    ref<T> a; T b;
    cat(ref<T> a, T b) : a(a), b(b) {}
    int size() const { return a.size + 1; }
    void copy(buffer<T>& target) const { target.append(a); target.append(b); }
    operator buffer<T>() const { buffer<T> target (size(), 0); copy(target); return move(target); }
};
generic cat<ref<T>, T, T> operator+(ref<T> a, T b) { return {a,b}; }

/// Concatenates a ref with a ref
generic struct cat<ref<T>, ref<T>, T> {
    ref<T> a; ref<T> b;
    cat(ref<T> a, ref<T> b) : a(a), b(b) {}
    int size() const { return a.size + b.size; }
    void copy(buffer<T>& target) const { target.append(a); target.append(b); }
    operator buffer<T>() const { buffer<T> target (size(), 0); copy(target); return move(target); }
};
generic cat<ref<T>,ref<T>, T> operator+(ref<T> a, ref<T> b) { return {a,b}; }
template<Type T, size_t N> cat<ref<T>,ref<T>, T> operator+(const T(&a)[N], ref<T> b) { return {a,b}; }
template<Type T, size_t N> cat<ref<T>,ref<T>, T> operator+(ref<T> a, const T(&b)[N]) { return {a,b}; }

/// Concatenates a ref with a buffer
generic struct cat<ref<T>, buffer<T>, T> {
    ref<T> a; buffer<T> b;
    no_copy(cat); cat(cat&& o) : a(move(o.a)), b(move(o.b)) {}
    cat(ref<T> a, buffer<T>&& b) : a(a), b(move(b)) {}
    int size() const { return a.size + b.size; }
    void copy(buffer<T>& target) const { target.append(a); target.append(b); }
    operator buffer<T>() const { buffer<T> target (size(), 0); copy(target); return move(target); }
};
generic cat<ref<T>,buffer<T>, T> operator+(ref<T> a, buffer<T>&& b) { return {a,move(b)}; }
template<Type T, size_t N> cat<ref<T>,buffer<T>, T> operator+(const T(&a)[N], buffer<T>&& b) { return {a,move(b)}; }

/// Concatenates a ref with a cat
template<Type B, Type T> struct cat<ref<T>, B, T> {
    const ref<T> a; B b;
    no_copy(cat); cat(cat&& o) : a(move(o.a)), b(move(o.b)) {}
    cat(ref<T> a, B&& b) : a(a), b(move(b)) {}
    int size() const { return a.size + b.size(); }
    void copy(buffer<T>& target) const { target.append(a); b.copy(target); }
    operator buffer<T>() const { buffer<T> target (size(), 0); copy(target); return move(target); }
};
template<Type T, Type A, Type B> cat<ref<T>, cat<A, B, T>, T> operator+(ref<T> a, cat<A, B, T>&& b) { return {a,move(b)}; }

/// Concatenates a buffer with a value
generic struct cat<buffer<T>, T, T> {
    buffer<T> a; T b;
    no_copy(cat); cat(cat&& o) : a(move(o.a)), b(move(o.b)) {}
    cat(buffer<T>&& a, T b) : a(move(a)), b(b) {}
    int size() const { return a.size + 1; }
    void copy(buffer<T>& target) const { target.append(a); target.append(b); }
    operator buffer<T>() const { buffer<T> target (size(), 0); copy(target); return move(target); }
};
generic cat<buffer<T>, T, T> operator+(buffer<T>&& a, T b) { return {move(a),b}; }

/// Concatenates a buffer with a ref
generic struct cat<buffer<T>, ref<T>, T> {
    buffer<T> a; ref<T> b;
    no_copy(cat); cat(cat&& o) : a(move(o.a)), b(move(o.b)) {}
    cat(buffer<T>&& a, ref<T> b) : a(move(a)), b(b) {}
    int size() const { return a.size + b.size; }
    void copy(buffer<T>& target) const { target.append(a); target.append(b); }
    operator buffer<T>() const { buffer<T> target (size(), 0); copy(target); return move(target); }
};
generic cat<buffer<T>,ref<T>, T> operator+(buffer<T>&& a, ref<T> b) { return {move(a),b}; }

/// Concatenates a buffer with an mref
generic struct cat<buffer<T>, mref<T>, T> {
    buffer<T> a; mref<T> b;
    no_copy(cat); cat(cat&& o) : a(move(o.a)), b(move(o.b)) {}
    cat(buffer<T>&& a, mref<T> b) : a(move(a)), b(b) {}
    int size() const { return a.size + b.size; }
    void copy(buffer<T>& target) const { target.append(a); target.append(b); }
    operator buffer<T>() const { buffer<T> target (size(), 0); copy(target); return move(target); }
};
generic cat<buffer<T>, mref<T>, T> operator+(buffer<T>&& a, mref<T> b) { return {move(a),b}; }

/// Concatenates a buffer with a buffer
generic struct cat<buffer<T>, buffer<T>, T> {
    buffer<T> a; buffer<T> b;
    no_copy(cat); cat(cat&& o) : a(move(o.a)), b(move(o.b)) {}
    cat(buffer<T>&& a, buffer<T>&& b) : a(move(a)), b(move(b)) {}
    int size() const { return a.size + b.size; }
    void copy(buffer<T>& target) const { target.append(a); target.append(b); }
    operator buffer<T>() const { buffer<T> target (size(), 0); copy(target); return move(target); }
};
generic cat<buffer<T>,buffer<T>, T> operator+(buffer<T>&& a, buffer<T>&& b) { return {move(a),move(b)}; }
