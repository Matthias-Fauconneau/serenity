#pragma once
#include "array.h"
#include "string.h"
#include "meta.h" //perfect forwarding
#include "debug.h"

template<class K, class V> struct const_pair { const K& key; const V& value; };
template<class K, class V> struct pair { K& key; V& value; };
/// \a map associates keys with values
template<class K, class V> struct map {
    array<K> keys;
    array<V> values;

    map()=default;
    map(map&&)=default;
    map& operator =(map&&)=default;
    void reserve(int size) { return keys.reserve(size); values.reserve(size); }
    uint size() const { return keys.size(); }
    bool contains(const K& key) const { return ::contains(keys, key); }
    explicit operator bool() const { return keys.size(); }
    void clear() { keys.clear(); values.clear(); }

    const V& at(const K& key) const { int i = indexOf(keys,key); assert(i>=0,"Invalid key"_,key,keys); return values[i];}
    V& at(const K& key) { int i = indexOf(keys, key); assert(i>=0,"Invalid key"_,key,keys); return values[i];}
    template<perfect(V)> Vf value(const K& key, Vf&& value) {
        int i = keys.indexOf(key);
        return i>=0 ? values[i] : forward<Vf>(value);
    }
    V* find(const K& key) { int i = indexOf(ref<K>(keys), key); return i>=0 ? &values[i] : 0; }
    template<perfect2(K,V)> void insert(Kf&& key, Vf&& value) {
        assert(!contains(key));
        //insertAt(values, insertSorted(keys, forward<Kf>(key)), forward<Vf>(value));
        keys << forward<Kf>(key); values << forward<Vf>(value);
    }
    template<perfect2(K,V)> void insertMulti(Kf&& key, Vf&& value) {
        //insertAt(values, insertSorted(keys, forward<Kf>(key)), forward<Vf>(value));
        keys << forward<Kf>(key); values << forward<Vf>(value);
    }
    template<perfect(K)> V& insert(Kf&& key) {
        assert(!contains(key));
        //return insertAt(values, insertSorted(keys, forward<Kf>(key)), move(V()));
        keys << forward<Kf>(key); values << V(); return values.last();
    }
    V& operator [](K key) { int i = indexOf(keys, key); if(i>=0) return values[i]; return insert(key); }
    void remove(const K& key) { int i=indexOf(keys, key); assert(i>=0); keys.removeAt(i); values.removeAt(i); }

    struct const_iterator {
        const K* k; const V* v;
        const_iterator(const K* k, const V* v) : k(k), v(v) {}
        bool operator!=(const const_iterator& o) const { return k != o.k; }
        const_pair<K,V> operator* () const { return i({*k,*v}); }
        const const_iterator& operator++ () { k++; v++; return *this; }
    };
    const_iterator begin() const { return const_iterator(keys.begin(),values.begin()); }
    const_iterator end() const { return const_iterator(keys.end(),values.end()); }

    struct iterator {
        K* k; V* v;
        iterator(K* k, V* v) : k(k), v(v) {}
        bool operator!=(const iterator& o) const { return k != o.k; }
        pair<K,V> operator* () const { return i({*k,*v}); }
        const iterator& operator++ () { k++; v++; return *this; }
    };
    iterator begin() { return iterator((K*)keys.begin(),(V*)values.begin()); }
    iterator end() { return iterator((K*)keys.end(),(V*)values.end()); }
};

template<class K, class V> inline map<K,V> copy(const map<K,V>& o) {
    map<K,V> t; t.keys=copy(o.keys); t.values=copy(o.values); return t;
}

template<class K, class V> inline string str(const map<K,V>& m) {
    string s("{"_);
    for(uint i=0;i<m.size();i++) { s<<str(m.keys[i])+": "_+str(m.values[i]); if(i<m.size()-1) s<<", "_; }
    return s+"}"_;
}
