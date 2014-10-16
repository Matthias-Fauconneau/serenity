#pragma once
#include "array.h"

/// Concatenates a \a cat with a cat
template<class A, class B, class T> struct cat {
    A a; B b;
    cat(A&& a, B&& b) : a(move(a)), b(move(b)) {}
    int size() const { return a.size() + b.size(); }
    void copy(Array<T>& target) const { a.copy(target); b.copy(target); }
    operator Array<T>() const { Array<T> target (size()); copy(target); return move(target); }
};
template<class T, class A, class B, class C, class D>
cat<cat<A, B, T>, cat<C, D, T>, T> operator+(cat<A, B, T>&& a, cat<C, D, T>&& b) { return {move(a),move(b)}; }

/// Concatenates a \a cat with a value
template<class A, class T> struct cat<A, T, T> {
    A a; const T b;
    cat(A&& a, const T b) : a(move(a)), b(b) {}
    int size() const { return a.size() + 1; };
    void copy(Array<T>& target) const { a.copy(target); target.append(b); }
    operator Array<T>() const { Array<T> target (size()); copy(target); return move(target); }
};
template<class T, class A, class B> cat<cat<A, B, T>, T, T> operator+(cat<A, B, T>&& a, T b) { return {move(a),b}; }

/// Concatenates a \a cat with a ref
template<class A, class T> struct cat<A, ref<T>, T> {
    A a; const ref<T> b;
    cat(A&& a, ref<T> b) : a(move(a)), b(b) {}
    int size() const { return a.size() + b.size; };
    void copy(Array<T>& target) const { a.copy(target); target.append(b); }
    operator Array<T>() const { Array<T> target (size()); copy(target); return move(target); }
};
template<class T, class A, class B> cat<cat<A, B, T>, ref<T>, T> operator+(cat<A, B, T>&& a, ref<T> b) { return {move(a),b}; }
// Required for implicit string literal conversion
template<class T, class A, class B, size_t N> cat<cat<A, B, T>, ref<T>, T> operator+(cat<A, B, T>&& a, const T(&b)[N]) { return {move(a),b}; }

/// Concatenates a \a cat with an Array
template<class A, class T> struct cat<A, Array<T>, T> {
    A a; Array<T> b;
    cat(A&& a, Array<T>&& b) : a(move(a)), b(move(b)) {}
    int size() const { return a.size() + b.size; };
    void copy(Array<T>& target) const { a.copy(target); target.append(b); }
    operator Array<T>() const { Array<T> target (size()); copy(target); return move(target); }
};
template<class T, class A, class B>
cat<cat<A, B, T>, Array<T>, T> operator+(cat<A, B, T>&& a, Array<T>&& b) { return {move(a),move(b)}; }

/// Concatenate a value with a ref
generic struct cat<T, ref<T>, T> {
    T a; ref<T> b;
    cat(T a, ref<T> b) : a(a), b(b) {}
    int size() const { return 1 + b.size; };
    void copy(Array<T>& target) const { target.append(a); target.append(b); }
    operator Array<T>() const { Array<T> target (size()); copy(target); return move(target); }
};
generic cat<T, ref<T>, T> operator+(T a, ref<T> b) { return {a,b}; }

/// Concatenates a ref with a value
generic struct cat<ref<T>, T, T> {
    ref<T> a; T b;
    cat(ref<T> a, T b) : a(a), b(b) {}
    int size() const { return a.size + 1; };
    void copy(Array<T>& target) const { target.append(a); target.append(b); }
    operator Array<T>() const { Array<T> target (size()); copy(target); return move(target); }
};
generic cat<ref<T>, T, T> operator+(ref<T> a, T b) { return {a,b}; }

/// Concatenates a ref with a ref
generic struct cat<ref<T>, ref<T>, T> {
    ref<T> a; ref<T> b;
    cat(ref<T> a, ref<T> b) : a(a), b(b) {}
    int size() const { return a.size + b.size; };
    void copy(Array<T>& target) const { target.append(a); target.append(b); }
    operator Array<T>() const { Array<T> target (size()); copy(target); return move(target); }
};
generic cat<ref<T>,ref<T>, T> operator+(ref<T> a, ref<T> b) { return {a,b}; }
template<class T, size_t N> cat<ref<T>,ref<T>, T> operator+(const T(&a)[N], ref<T> b) { return {a,b}; }
template<class T, size_t N> cat<ref<T>,ref<T>, T> operator+(ref<T> a, const T(&b)[N]) { return {a,b}; }

/// Concatenates a ref with an Array
generic struct cat<ref<T>, Array<T>, T> {
    ref<T> a; Array<T> b;
    cat(ref<T> a, Array<T>&& b) : a(a), b(move(b)) {}
    int size() const { return a.size + b.size; };
    void copy(Array<T>& target) const { target.append(a); target.append(b); }
    operator Array<T>() const { Array<T> target (size()); copy(target); return move(target); }
};
generic cat<ref<T>,Array<T>, T> operator+(ref<T> a, Array<T>&& b) { return {a,move(b)}; }
template<class T, size_t N> cat<ref<T>,Array<T>, T> operator+(const T(&a)[N], Array<T>&& b) { return {a,move(b)}; }

/// Concatenates a ref with a cat
template<class B, class T> struct cat<ref<T>, B, T> {
    const ref<T> a; B b;
    cat(ref<T> a, B&& b) : a(a), b(move(b)) {}
    int size() const { return a.size + b.size(); };
    void copy(Array<T>& target) const { target.append(a); b.copy(target); }
    operator Array<T>() const { Array<T> target (size()); copy(target); return move(target); }
    //operator ref<T>() const { return Array<T>(); }
};
template<class T, class A, class B> cat<ref<T>, cat<A, B, T>, T> operator+(ref<T> a, cat<A, B, T>&& b) { return {a,move(b)}; }

/// Concatenates an Array with a value
generic struct cat<Array<T>, T, T> {
    Array<T> a; T b;
    cat(Array<T>&& a, T b) : a(move(a)), b(b) {}
    int size() const { return a.size + 1; };
    void copy(Array<T>& target) const { target.append(a); target.append(b); }
    operator Array<T>() const { Array<T> target (size()); copy(target); return move(target); }
};
generic cat<Array<T>, T, T> operator+(Array<T>&& a, T b) { return {move(a),b}; }

/// Concatenates an Array with a ref
generic struct cat<Array<T>, ref<T>, T> {
    Array<T> a; ref<T> b;
    cat(Array<T>&& a, ref<T> b) : a(move(a)), b(b) {}
    int size() const { return a.size + b.size; };
    void copy(Array<T>& target) const { target.append(a); target.append(b); }
    operator Array<T>() const { Array<T> target (size()); copy(target); return move(target); }
};
generic cat<Array<T>,ref<T>, T> operator+(Array<T>&& a, ref<T> b) { return {move(a),b}; }
// Required for implicit string literal conversion
template<class T, size_t N> cat<Array<T>,ref<T>, T> operator+(Array<T>&& a, const T(&b)[N]) { return {move(a),b}; }

/// Concatenates an Array with an Array
generic struct cat<Array<T>, Array<T>, T> {
    Array<T> a; Array<T> b;
    cat(Array<T>&& a, Array<T>&& b) : a(move(a)), b(move(b)) {}
    int size() const { return a.size + b.size; };
    void copy(Array<T>& target) const { target.append(a); target.append(b); }
    operator Array<T>() const { Array<T> target (size()); copy(target); return move(target); }
};
generic cat<Array<T>,Array<T>, T> operator+(Array<T>&& a, Array<T>&& b) { return {move(a),move(b)}; }
