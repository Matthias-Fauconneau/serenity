#pragma once
/// \file map.h Associative array (using linear search)
#include "array.h"
#include "string.h"

template<Type K, Type V> struct const_entry { const K& key; const V& value; };
template<Type K, Type V> struct entry { K& key; V& value; };

/// Associates keys with values
template<Type K, Type V> struct map {
 array<K> keys;
 array<V> values;

 map(){}
 map(buffer<K>&& keys, const mref<V> values) : keys(move(keys)), values(moveRef(values)) { assert_(keys.size==values.size, keys.size, values.size); }
 map(buffer<K>&& keys, buffer<V>&& values) : keys(move(keys)), values(move(values)) { assert_(keys.size==values.size, keys.size, values.size); }

 size_t size() const { return keys.size; }
 size_t count() const { return keys.size; }
 void reserve(int size) { keys.reserve(size); values.reserve(size); }
 void clear() { keys.clear(); values.clear(); }

 explicit operator bool() const { return keys.size; }
 bool operator ==(const map<K,V>& o) const { return keys==o.keys && values==o.values; }

 template<Type KK> bool contains(const KK& key) const { return keys.contains(key); }
 template<Type KK> void assertNo(unused const KK& key) const { assert_(!contains(key), '\''+str(key)+'\'',"already in",keys); }

 template<Type KK> size_t indexOf(const KK& key) const {
  size_t i = keys.indexOf(key);
  if(i==invalid) error('\''+str(key)+'\'',"not in",keys);
  return i;
 }

 /// Returns whether this object has the same values for all keys in subsets.
 /// Missing keys fail if subset value is not null
 bool includes(const map<K, V>& subset) const {
  for(auto entry: subset) {
   if(contains(entry.key)) { if(at(entry.key) != entry.value) return false; }
   else if(entry.value) return false;
  }
  return true;
 }

 /// Returns whether this object has the same values for all keys in subsets.
 /// Missing keys always pass regardless of subset value
 bool includesPassMissing(const map<K, V>& subset) const {
  for(auto entry: subset) {
   if(!contains(entry.key) || !at(entry.key)) continue;
   if(at(entry.key) != entry.value) return false;
  }
  return true;
 }

 template<Type KK> const V& at(const KK& key) const { return values[indexOf(key)]; }
 template<Type KK> V& at(const KK& key) { return values[indexOf(key)]; }

 template<Type KK, Type VV> VV value(const KK& key, VV&& value) const {
  size_t i = keys.indexOf(key);
  return i!=invalid ? VV(values[i]) : forward<VV>(value);
 }
 template<Type KK> const V value(const KK& key, const V& value=V()) const {
  size_t i = keys.indexOf(key);
  return i!=invalid ? values[i] : value;
 }

 template<Type KK> const V* find(const KK& key) const { size_t i = keys.indexOf(key); return i!=invalid ? &values[i] : 0; }
 template<Type KK> V* find(const KK& key) { size_t i = keys.indexOf(key); return i!=invalid ? &values[i] : 0; }

 template<Type KK> V& insert(KK&& key) { assertNo(key); keys.append(move(key)); return values.append(); }
 template<Type KK, Type VV> V& insert(KK&& key, VV&& value) {
  assertNo(key);
  keys.append(forward<KK>(key));
  return values.append(forward<VV>(value));
 }
 template<Type KK> V& insert(KK&& key, V&& value) { return insert<KK,V>(forward<KK>(key), move(value)); }
 template<Type KK, Type VV> V& insertMulti(KK&& key, VV&& value) {
  keys.append(forward<KK>(key)); return values.append(forward<VV>(value));
 }
 template<Type KK> V& insertSorted(KK&& key) { assertNo(key); return values.insertAt(keys.insertSorted(move(key)), V()); }
 template<Type KK> V& insertSorted(const KK& key, const V& value) { assertNo(key); return values.insertAt(keys.insertSorted(key),value); }
 template<Type KK> V& insertSorted(const KK& key, V&& value) {
  assertNo(key);
  return values.insertAt(keys.insertSorted(key),move(value));
 }
 V& insertSortedMulti(K&& key, V&& value) { return values.insertAt(keys.insertSorted(::move(key)),::move(value)); }
 V& insertSortedMulti(const K& key, V&& value) { return values.insertAt(keys.insertSorted(key),::move(value)); }
 V& insertSortedMulti(K&& key, const V& value) { return values.insertAt(keys.insertSorted(::move(key)),value); }
 V& insertSortedMulti(const K& key, const V& value) { return values.insertAt(keys.insertSorted(key),value); }

 template<Type KK> V& operator [](KK&& key) { size_t i = keys.indexOf(key); return i!=invalid ? values[i] : insertSorted(key); }
 /// Returns value for \a key, inserts a new sorted key with a default value if not existing
 template<Type KK> V& sorted(const KK& key) {
  size_t i = keys.indexOf(key); if(i!=invalid) return values[i];
  return values.insertAt(keys.insertSorted(key),V());
 }

 template<Type KK> V take(const KK& key) { size_t i=indexOf(key); keys.removeAt(i); return values.take(i); }
 template<Type KK> void remove(const KK& key) { size_t i=indexOf(key); keys.removeAt(i); values.removeAt(i); }

 struct const_iterator {
  const K* k; const V* v;
  const_iterator(const K* k, const V* v) : k(k), v(v) {}
  bool operator!=(const const_iterator& o) const { return k != o.k; }
  const_entry<K,V> operator* () const { return {*k,*v}; }
  const const_iterator& operator++ () { k++; v++; return *this; }
 };
 const_iterator begin() const { return const_iterator(keys.begin(),values.begin()); }
 const_iterator end() const { return const_iterator(keys.end(),values.end()); }

 struct iterator {
  K* k; V* v;
  iterator(K* k, V* v) : k(k), v(v) {}
  bool operator!=(const iterator& o) const { return k != o.k; }
  entry<K,V> operator* () const { return {*k,*v}; }
  const iterator& operator++ () { k++; v++; return *this; }
 };
 iterator begin() { return iterator(keys.begin(),values.begin()); }
 iterator end() { return iterator(keys.end(),values.end()); }

 template<Type F> map& filter(F f) { for(size_t i=0; i<size();) if(f(keys[i], values[i])) { keys.removeAt(i); values.removeAt(i); } else i++; return *this; }

 void append(map&& b) { for(entry<K, V> e: b) { insert(move(e.key), move(e.value)); } }
 void appendReplace(map&& b) { for(entry<K, V> e: b) { operator[](move(e.key)) = move(e.value); } }

 void appendMulti(const map& b) { for(const_entry<K, V> e: b) { insertMulti(copy(e.key), copy(e.value)); } }
 void appendMulti(map&& b) { for(entry<K, V> e: b) { insertMulti(move(e.key), move(e.value)); } }
};

template<Type K, Type V> map<K,V> copy(const map<K,V>& o) {
 map<K,V> t; t.keys=copy(o.keys); t.values=copy(o.values); return t;
}

template<Type K, Type V> String str(const map<K,V>& m, string separator=","_) {
 array<char> s;
 //s.append('{');
 //s.append(separator.last());
 for(uint i: range(m.size())) {
  s.append(str(m.keys[i])+"="+str(m.values[i])); // = instead of : for compatibility as SGE job name
  if(i<m.size()-1) s.append(separator);
 }
 //s.append(separator.last());
 //s.append('}');
 return move(s);
}

/// Associates each argument's name with its string conversion
template<Type... Args> map<string,String> withName(string names, const Args&... args) { log(names, split(names,", "_)); return map<string,String>(split(names,", "_),{str(args)...}); }
#define withName(args...) withName(#args, args)
