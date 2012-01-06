#pragma once
#include "core.h"
#include <initializer_list>

template <class T> struct array {
	array(){}
	array(array& o) = delete;
	array& operator=(const array&) = delete;

	/// move constructor
	array(array&& o) : data(o.data), size(o.size), capacity(o.capacity) { o.capacity=0; }
	/*/// move constructor with conversion
	template <class O> explicit array(array<O>&& o)
	: data((const T*)o.data), size(o.size*sizeof(O)/sizeof(T)), capacity(o.capacity*sizeof(O)/sizeof(T)) { o.capacity=0; }*/
	/// move assigment
	array& operator=(array&& o) { this->~array(); data=o.data; size=o.size; capacity=o.capacity; o.capacity=0; return *this; }
	/// allocate a new uninitialized array for \a capacity elements
	explicit array(int capacity) : data((T*)malloc((size_t)capacity*sizeof(T))), capacity(capacity) { assert(capacity>0); }
	/// allocate a new array with \a size elements initialized to \a value
	explicit array(int size, const T& value) : data((T*)malloc(size*sizeof(T))), capacity(size) { for(int i=0;i<size;i++) append(value); }
	/// reference elements from an initalizer \a list
	array(const std::initializer_list<T>& list) : data((T*)list.begin()), size((int)list.size()) {}
	/// reference elements from null terminated \a data
	array(const T* data) : data(data) { assert(data); while(data[size]!=0) size++; }
	/// reference \a size elements from existing \a data
	explicit array(const T* data, int size) : data(data), size(size) { assert(size>=0); assert(data); }
	/// reference elements sliced from \a begin to \a end
	explicit array(const T* begin,const T* end) : data(begin), size(int(end-begin)) { assert(size>=0); assert(data); }
	/// if \a this own the data, destroy all initialized elements and free the buffer
	~array() { if(capacity) { for(int i=0;i<size;i++) data[i].~T(); free((void*)data); } }
	/// allocate enough space for \a capacity elements
	void reserve(int capacity) {
		if(this->capacity>=capacity) return;
		if(this->capacity) data=(T*)realloc((void*)data,(size_t)capacity*sizeof(T));
		else if(capacity) { T* copy=(T*)malloc((size_t)capacity*sizeof(T)); ::copy(copy,data,size); data=copy; }
		this->capacity=capacity;
	}
	/// make sure data is an owned reference (deep copy)
	void detach() { assert(size); if(!capacity) reserve(size); }
	/// reduce size and destroy removed elements
	void shrink(int size) { assert(size<this->size); if(capacity) for(int i=size;i<this->size;i++) data[i].~T(); this->size=size; }
	/// reserve space and initialize added elements
	void grow(int size) { assert(size>this->size); reserve(size); for(int i=this->size;i<size;i++) new ((void*)&data[i])T(); this->size=size; }
	/// resize array to \a size, destroy/initialize elements as needed
	void resize(int size) { if(size<this->size) shrink(size); else if(size>this->size) grow(size); }
	/// resize array to 0, destroying any contained elements
	void clear() { if(size) shrink(0); }
	/// slice an array referencing elements from \a pos to \a pos + \a size
	array<T> slice(int pos,int size) const { assert(pos>=0 && pos+size<=this->size); return array<T>(data+pos,size); }
	/// slice an array referencing elements from \a pos to the end of the array
	array<T> slice(int pos) const { return slice(pos,size-pos); }
	/// copy all elements to \a buffer
	void copy(T* dst) const { ::copy(dst,data,size); }
	/// copy all elements in a new array
	array<T> copy() const { array<T> r(size); copy((T*)r.data); r.size=size; return r; }

	/// element
	const T& at(int i) const { assert(i>=0 && i<size,"i",i,"size",size); return data[i]; }
	const T& operator [](int i) const { return at(i); }
	const T& first() const { return at(0); }
	const T& last() const { return at(size-1); }
	T& at(int i) { assert(i>=0 && i<size,"i",i,"size",size); return (T&)data[i]; }
	T& operator [](int i) { detach(); return at(i); }
	T& first() { detach(); return at(0); }
	T& last() { detach(); return at(size-1); }

	/// query
	int indexOf(const T& v) const { for(int i=0;i<size;i++) { if( data[i]==v ) return i; } return -1; }
	bool contains(const T& v) const { return indexOf(v)>=0; }
	/*bool contains(const array<T>& a) const {
		for(int i=0;i<size-a.size;i++) {
			for(int j=0;j<a.size;j++) {
				if(at(i+j)!=a[j]) goto next;
			}
			return true;
			next:;
		}
		return false;
	}*/
	bool startsWith(const array<T>& a) const {
		if(size < a.size) return false;
		for(int i=0;i<a.size;i++) if(at(i)!=a[i]) return false;
		return true;
	}
	bool endsWith(const array<T>& a) const {
		if(size < a.size) return false;
		for(int i=0;i<a.size;i++) if(at(i+size-a.size)!=a[i]) return false;
		return true;
	}
	bool operator ==(const array<T>& a) const {
		if(size != a.size) return false;
		for(int i=0;i<size;i++) if(!(at(i)==a[i])) return false;
		return true;
	}
	bool operator !=(const array<T>& a) const { return !(*this==a); }
	explicit operator bool() const { return size; }

	/// remove
	void removeAt(int i) { detach(); for(;i<size-1;i++) ::copy(at(i),at(i+1)); size--; }
	void removeLast() { assert(size); removeAt(size-1); }
	void removeOne(T v) { int i=indexOf(v); if(i>=0) removeAt(i); }
	T take(int i) { T value = move(at(i)); removeAt(i); return value; }
	T takeFirst() { return take(0); }
	T takeLast() { return take(size-1); }

	/// append
	perfect(T) void append(Tf&& v) { int s=size+1; reserve(s); new ((void*)&data[size]) T(forward<Tf>(v)); size=s; }
	perfect(T) void appendOnce(Tf&& v) { if(!contains(v)) append(forward<Tf>(v)); }
	perfect(T) array<T>& operator <<(Tf&& v) { append(forward<Tf>(v)); return *this; }
	void append(array<T>&& a) { int s=size+a.size; reserve(s); for(int i=0;size<s;size++,i++) new ((void*)&data[size]) T(move(a[i])); }
	array<T>& operator <<(array<T>&& v) { append(move(v)); return *this; }

	/// insert
	perfect(T) void insertAt(int index, Tf&& v) {
		reserve(size+1); size++;
		for(int i=size-2;i>=index;i--) ::copy(at(i+1),at(i));
		new ((void*)&data[index]) T(forward<Tf>(v));
	}
	perfect(T) void insertSorted(Tf&& v) {
		int i=0;
		for(;i<size;i++) if(v < at(i)) break;
		insertAt(i,forward<Tf>(v));
	}
	void insertSorted(array<T>&& a) {
		int s=size+a.size; reserve(s);
		for(auto&& e: a) insertSorted(move(e));
	}

	/// operations
	void reverse() { for(int i=0; i<size/2; i++) swap(at(i), at(size-i-1)); }
	array<T> replace(T before, T after) const {
		array<T> r; r.resize(size); T* d=(T*)r.data; for(int i=0;i<size;i++) d[i] = data[i] == before ? after : data[i]; return r;
	}

	/// iterators
	const T* begin() const { return data; }
	const T* end() const { return &data[size]; }
	T* begin() { if(size) detach(); return (T*)data; }
	T* end() { return (T*)&data[size]; }

	const T* data = 0;
	int size = 0;
	int capacity = 0; //0 = not owned
};

template<class T> void log_(const array<T>& a, const char* sep=", ") {
	//log_('[');
	for(int i=0;i<a.size;i++) { log_(a[i]); if(i<a.size-1) log_(sep); }
	//log_(']');
}
