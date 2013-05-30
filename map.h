#pragma once
/// \file map.h Associative array (using linear search)
#include "array.h"
#include "string.h"

template<Type K, Type V> struct const_pair { const K& key; const V& value; };
template<Type K, Type V> struct pair { K& key; V& value; };
template<Type K, Type V> struct key_value { K key; V value; };
/// Associates keys with values
template<Type K, Type V> struct map {
    map(){}
    map(const ref<key_value<K,V>>& pairs){ for(const key_value<K,V>& pair: pairs) keys<<pair.key, values<<pair.value; }
    map(const ref<K>& keys, const ref<V>& values):keys(keys),values(values){ assert(keys.size==values.size); }

    uint size() const { return keys.size; }
    void reserve(int size) { return keys.reserve(size); values.reserve(size); }
    void clear() { keys.clear(); values.clear(); }

    explicit operator bool() const { return keys.size; }
    bool operator ==(const map<K,V>& o) const { return keys==o.keys && values==o.values; }

    template<Type KK> bool contains(const KK& key) const { return keys.contains(key); }

    template<Type KK> const V& at(const KK& key) const {
        int i = keys.indexOf(key);
        if(i<0) error("'"_+str(key)+"' not in {"_,keys,"}"_);
        return values[i];
    }
    template<Type KK> V& at(const KK& key) {
        int i = keys.indexOf(key);
        if(i<0) error("'"_+str(key)+"' not in {"_,keys,"}"_);
        return values[i];
    }

    const V& value(const K& key, V&& value) const {
        int i = keys.indexOf(key);
        return i>=0 ? values[i] : value;
    }
    V value(const K& key, const V& value) const {
        int i = keys.indexOf(key);
        return i>=0 ? values[i] : value;
    }

    V* find(const K& key) { int i = keys.indexOf(key); return i>=0 ? &values[i] : 0; }

    V& insert(K&& key) { assert(!contains(key),key); keys << move(key); values << V(); return values.last(); }
    V& insert(const K& key) { assert(!contains(key),key); keys << key; values << V(); return values.last(); }

    V& insert(K&& key, V&& value) {
        if(contains(key)) error("'"_+str(key)+"' already in {"_,keys,"}"_);
        keys << move(key); values << move(value); return values.last();
    }
    V& insert(K&& key, const V& value) {
        if(contains(key)) error("'"_+str(key)+"' already in {"_,keys,"}"_);
        keys << move(key); values << value; return values.last();
    }
    V& insert(const K& key, V&& value) {
        if(contains(key)) error("'"_+str(key)+"' already in {"_,keys,"}"_);
        keys << key; values << move(value); return values.last();
    }
    V& insert(const K& key, const V& value) {
        if(contains(key)) error("'"_+str(key)+"' already in {"_,keys,"}"_);
        keys << key; values << value; return values.last();
    }
    V& insertMulti(const K& key, const V& value) {
        keys << key; values << value; return values.last();
    }
    V& insertSorted(const K& key, const V& value) {
        if(contains(key)) error("'"_+str(key)+"' already in {"_,keys,"}"_);
        return  values.insertAt(keys.insertSorted(key),value);
    }
    V& insertSortedMulti(const K& key, const V& value) {
        return  values.insertAt(keys.insertSorted(key),value);
    }

    V& operator [](K key) { int i = keys.indexOf(key); if(i>=0) return values[i]; return insert(key); }
    /// Returns value for \a key, inserts a new sorted key with a default value if not existing
    V& sorted(const K& key) {
        int i = keys.indexOf(key); if(i>=0) return values[i];
        return values.insertAt(keys.insertSorted(key),V());
    }

    V take(const K& key) { int i=keys.indexOf(key); if(i<0) error("'"_+str(key)+"' not in {"_,keys,"}"_); keys.removeAt(i); return values.take(i); }
    void remove(const K& key) { int i=keys.indexOf(key); assert(i>=0); keys.removeAt(i); values.removeAt(i); }

    struct const_iterator {
        const K* k; const V* v;
        const_iterator(const K* k, const V* v) : k(k), v(v) {}
        bool operator!=(const const_iterator& o) const { return k != o.k; }
        const_pair<K,V> operator* () const { return {*k,*v}; }
        const const_iterator& operator++ () { k++; v++; return *this; }
    };
    const_iterator begin() const { return const_iterator(keys.begin(),values.begin()); }
    const_iterator end() const { return const_iterator(keys.end(),values.end()); }

    struct iterator {
        K* k; V* v;
        iterator(K* k, V* v) : k(k), v(v) {}
        bool operator!=(const iterator& o) const { return k != o.k; }
        pair<K,V> operator* () const { return {*k,*v}; }
        const iterator& operator++ () { k++; v++; return *this; }
    };
    iterator begin() { return iterator((K*)keys.begin(),(V*)values.begin()); }
    iterator end() { return iterator((K*)keys.end(),(V*)values.end()); }

    array<K> keys;
    array<V> values;
};

template<Type K, Type V> map<K,V> copy(const map<K,V>& o) {
    map<K,V> t; t.keys=copy(o.keys); t.values=copy(o.values); return t;
}

template<Type K, Type V> string str(const map<K,V>& m) {
    string s; s<<'{'; for(uint i: range(m.size())) { s<<str(m.keys[i])<<": "_<<str(m.values[i]); if(i<m.size()-1) s<<", "_; } s<<'}'; return s;
}
template<Type K, Type V> string toASCII(const map<K,V>& m) {
        string s; for(uint i: range(m.size())) { s<<str(m.keys[i]); if(m.values[i]) s<<':'<<str(m.values[i]); if(i<m.size()-1) s<<','; } return s;
}

template<Type K, Type V> map<K,V> operator<<(const map<K,V>& a, const map<K,V>& b) {
    map<K, V> r = copy(a);
    for(const_pair<K, V> e: b) { assert(!r.contains(e.key), r.at(e.key), e.value, a, b); r.insert(e.key, copy(e.value)); }
    return r;
}
