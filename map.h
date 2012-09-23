#pragma once
#include "array.h"
#include "meta.h" //perfect forwarding
#include "string.h"

template<class K, class V> struct const_pair { const K& key; const V& value; };
template<class K, class V> struct pair { K& key; V& value; };
/// \a map associates keys with values
template<class K, class V> struct map {
    array<K> keys;
    array<V> values;

    void reserve(int size) { return keys.reserve(size); values.reserve(size); }
    uint size() const { return keys.size(); }
    bool contains(const K& key) const { return keys.contains(key); }
    explicit operator bool() const { return keys.size(); }
    void clear() { keys.clear(); values.clear(); }

    const V& at(const K& key) const { int i = keys.indexOf(key); if(i<0)error("'"_+str(key)+"' not in {"_,keys,"}"_); return values[i];}
    V& at(const K& key) { int i = keys.indexOf(key); if(i<0)error("'"_+str(key)+"' not in {"_,keys,"}"_); return values[i];}
    template<class... Args> V value(const K& key, Args&&... args) {
        int i = keys.indexOf(key);
        return i>=0 ? values[i] : V(forward<Args>(args)___);
    }
    V* find(const K& key) { int i = keys.indexOf(key); return i>=0 ? &values[i] : 0; }
    template<perfect2(K,V)>
    V& insert(Kf&& key, Vf&& value) {
        if(contains(key)) error("'"_+str(key)+"' already in {'"_,keys,"}"_);
        keys << forward<Kf>(key); values << forward<Vf>(value); return values.last();
    }
    template<perfect2(K,V)>
    V& insertMulti(Kf&& key, Vf&& value) {
        keys << forward<Kf>(key); values << forward<Vf>(value); return values.last();
    }
    /// Returns value for \a key, inserts a new sorted key with a default value if not existing
    template<perfect(K)> V& sorted(Kf&& key) {
        int i = keys.indexOf(key); if(i>=0) return values[i];
        return values.insertAt(keys.insertSorted(key),V());
    }
    template<perfect(K)> V& insert(Kf&& key) {
        assert(!contains(key),key);
        keys << forward<Kf>(key); values << V(); return values.last();
    }
    V& operator [](K key) { int i = keys.indexOf(key); if(i>=0) return values[i]; return insert(key); }
    void remove(const K& key) { int i=keys.indexOf(key); assert(i>=0); keys.removeAt(i); values.removeAt(i); }

    struct const_iterator {
        const K* k; const V* v;
        const_iterator(const K* k, const V* v) : k(k), v(v) {}
        bool operator!=(const const_iterator& o) const { return k != o.k; }
        const_pair<K,V> operator* () const { return __(*k,*v); }
        const const_iterator& operator++ () { k++; v++; return *this; }
    };
    const_iterator begin() const { return const_iterator(keys.begin(),values.begin()); }
    const_iterator end() const { return const_iterator(keys.end(),values.end()); }

    struct iterator {
        K* k; V* v;
        iterator(K* k, V* v) : k(k), v(v) {}
        bool operator!=(const iterator& o) const { return k != o.k; }
        pair<K,V> operator* () const { return __(*k,*v); }
        const iterator& operator++ () { k++; v++; return *this; }
    };
    iterator begin() { return iterator((K*)keys.begin(),(V*)values.begin()); }
    iterator end() { return iterator((K*)keys.end(),(V*)values.end()); }
};

template<class K, class V> map<K,V> copy(const map<K,V>& o) {
    map<K,V> t; t.keys=copy(o.keys); t.values=copy(o.values); return t;
}

template<class K, class V> string str(const map<K,V>& m) {
    string s; s<<'{'; for(uint i=0;i<m.size();i++) { s<<str(m.keys[i])<<": "_<<str(m.values[i]); if(i<m.size()-1) s<<", "_; } s<<'}'; return s;
}
