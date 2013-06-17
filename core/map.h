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

    template<Type KK> V value(const KK& key, V&& value) const {
        int i = keys.indexOf(key);
        return i>=0 ? copy(values[i]) : move(value);
    }
    template<Type KK> const V& value(const KK& key, const V& value=V()) const {
        int i = keys.indexOf(key);
        return i>=0 ? values[i] : value;
    }

    template<Type KK> V* find(const KK& key) { int i = keys.indexOf(key); return i>=0 ? &values[i] : 0; }
    template<Type KK> V& insert(KK&& key) { assert(!contains(key),key); keys << K(move(key)); values << V(); return values.last(); }

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
    template<Type KK> V& insertMulti(const KK& key, const V& value) {
        keys << key; values << value; return values.last();
    }
    template<Type KK> V& insertSorted(const KK& key, const V& value) {
        if(contains(key)) error("'"_+str(key)+"' already in {"_,keys,"}"_);
        return values.insertAt(keys.insertSorted(key),value);
    }
    template<Type KK> V& insertSortedMulti(const KK& key, const V& value) {
        return values.insertAt(keys.insertSorted(key),value);
    }

    template<Type KK> V& operator [](KK key) { int i = keys.indexOf(key); if(i>=0) return values[i]; return insert(key); }
    /// Returns value for \a key, inserts a new sorted key with a default value if not existing
    template<Type KK> V& sorted(const KK& key) {
        int i = keys.indexOf(key); if(i>=0) return values[i];
        return values.insertAt(keys.insertSorted(key),V());
    }

    template<Type KK> V take(const KK& key) { int i=keys.indexOf(key); if(i<0) error("'"_+str(key)+"' not in {"_,keys,"}"_); keys.removeAt(i); return values.take(i); }
    template<Type KK> void remove(const KK& key) { int i=keys.indexOf(key); if(i<0) error("'"_+str(key)+"' not in {"_,keys,"}"_); keys.removeAt(i); values.removeAt(i); }

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

template<Type K, Type V> String str(const map<K,V>& m) {
    String s; s<<'{'; for(uint i: range(m.size())) { s<<str(m.keys[i]); if(m.values[i]) s<<": "_<<str(m.values[i]); if(i<m.size()-1) s<<", "_; } s<<'}'; return s;
}
template<Type K, Type V> String toASCII(const map<K,V>& m) {
        String s; for(uint i: range(m.size())) { s<<str(m.keys[i]); if(m.values[i]) s<<':'<<str(m.values[i]); if(i<m.size()-1) s<<'|'; } return replace(move(s),'/','\\');
}

template<Type K, Type V> void operator<<(map<K,V>& a, const map<K,V>& b) {
    for(const_pair<K, V> e: b) { assert_(!a.contains(e.key), a.at(e.key), e.value, a, b); a.insert(copy(e.key), copy(e.value)); }
}
