#pragma once
#include <initializer_list>

template <class T> struct array {
	array() = default;
	array(array& o) = delete;
	array& operator=(const array&) = delete;

	/// move constructor
	array(array&& o) : data(o.data), size(o.size), capacity(o.capacity) { o.capacity=0; }
	/// move constructor with conversion
	template <class O> explicit array(array<O>&& o) :
		data((const T*)o.data), size(o.size*sizeof(O)/sizeof(T)), capacity(o.capacity*sizeof(O)/sizeof(T)) { o.capacity=0; }
	/// move assigment
	array& operator=(array&& o)
		{ this->~array(); data=o.data; size=o.size; capacity=o.capacity; o.capacity=0; return *this; }
	/// allocate a new uninitialized array for \a capacity elements
	explicit array(int capacity) :
		data((T*)malloc((size_t)capacity*sizeof(T))), capacity(capacity) { assert(capacity>0); }
	/// allocate a new array with \a size elements initialized to \a value
	explicit array(int size, const T& value) :
		data((T*)malloc(size*sizeof(T))), capacity(size) { assert(size>0); for(int i=0;i<size;i++) append(value); }
	/// reference elements from an initalizer \a list
	explicit array(const std::initializer_list<T>& list) :
		data((T*)list.begin()), size((int)list.size()) {}
	/// reference \a size elements from existing buffer \a data
	explicit array(const T* data, int size)
			 : data(data), size(size) { assert(size>=0); assert(data); }
	/// reference elements sliced from \a begin to \a end
	explicit array(const T* begin,const T* end)
	: data(begin), size(int(end-begin)) { assert(size>=0); assert(data); }
	/// if \a this own the data, destroy all initialized elements and free the buffer
	~array() { if(capacity) { for(int i=0;i<size;i++) data[i].~T(); free((void*)data); } }

	void reserve(int new_capacity) {
		if(capacity>=new_capacity) return;
		if(capacity) data=(T*)realloc((void*)data,(size_t)new_capacity*sizeof(T));
		else if(new_capacity) { T* copy=(T*)malloc((size_t)new_capacity*sizeof(T)); ::copy(copy,data,size); data=copy; }
		capacity=new_capacity;
	}
	void shrink(int new_size) { assert(new_size<=size); if(capacity) for(int i=new_size;i<size;i++) data[i].~T(); size=new_size; }
	void resize(int new_size) {
		if(new_size<size) shrink(new_size);
		else if(new_size>size) {
			reserve(new_size);
			for(int i=size;i<new_size;i++) new ((void*)&data[i])T();
			size=new_size;
		}
	}
	bool isSlice() { return !capacity; }
	void detach() { if(size) reserve(size); }
	void clear() { shrink(0); }
	void copy(T* dst) const { ::copy(dst,data,size); }

	array<T> slice(int pos,int size) const { assert(pos>=0 && pos+size<=this->size); return array<T>(data+pos,size); }
	array<T> slice(int pos) const { return slice(pos,size-pos); }

	/// element
	const T& at(int i) const { assert(i>=0 && i<size); return data[i]; }
	const T& operator [](int i) const { return at(i); }
	const T& first() const { return at(0); }
	const T& last() const { return at(size-1); }
	T& at(int i) { assert(i>=0 && i<size); return (T&)data[i]; }
	T& operator [](int i) { detach(); return at(i); }
	T& first() { detach(); return at(0); }
	T& last() { detach(); return at(size-1); }
	T take(int i) { T value = move(at(i)); removeAt(i); return value; }
	T takeFirst() { return take(0); }
	T takeLast() { return take(size-1); }

	/// query
	int indexOf(const T& v) const { for(int i=0;i<size;i++) { if( data[i]==v ) return i; } return -1; }
	bool contains(const T& v) const { return indexOf(v)>=0; }
	bool contains(const array<T>& a) const {
		for(int i=0;i<size-a.size;i++) {
			for(int j=0;j<a.size;j++) {
				if(at(i+j)!=a[j]) goto next;
			}
			return true;
			next:;
		}
		return false;
	}
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
		for(int i=0;i<size;i++) if(at(i)!=a[i]) return false;
		return true;
	}
	bool operator !=(const array<T>& a) const { return !(*this==a); }
	explicit operator bool() const { return size; }

	/// remove
	void removeAt(int i) { detach(); size--; memmove((void*)(data+i),data+i+1,size_t(size-i));}
	void removeLast() { removeAt(size-1); }
	void removeOne(T v) { int i=indexOf(v); assert(i>=0); removeAt(i); }

	/// insert
	perfect(T) void append(Tf&& v) { int s=size+1; reserve(s); new ((void*)&data[size]) T(forward<Tf>(v)); size=s; }
	void append(array<T>&& a) { int s=size+a.size; reserve(s); for(int i=0;size<s;size++,i++) new ((void*)&data[size]) T(move(a[i])); }
	perfect(T) void appendOnce(Tf&& v) { if(!contains(v)) append(forward<Tf>(v)); }
	perfect(T) array<T>& operator <<(Tf&& v) { append(forward<Tf>(v)); return *this; }
	array<T>& operator <<(array<T>&& v) { append(move(v)); return *this; }

	/// iterators
	struct const_iterator {
		const T* t;
		const_iterator(const T* t) : t(t) {}
		bool operator!=(const const_iterator& o) const { return t != o.t; }
		const T& operator* () const { return *t; }
		const const_iterator& operator++ () { t++; return *this; }
	};
	const_iterator begin() const { return const_iterator(data); }
	const_iterator end() const { return const_iterator(&data[size]); }
	struct iterator {
		T* t;
		iterator(T* t) : t(t) {}
		bool operator!=(const iterator& o) const { return t != o.t; }
		T& operator* () const { return *t; }
		const iterator& operator++ () { t++; return *this; }
	};
	iterator begin() { detach(); return iterator((T*)data); }
	iterator end() { return iterator((T*)&data[size]); }

	public:
	const T* data = 0;
	int size = 0;
	int capacity = 0; //0 = not owned
};

template <class A, class T> struct cat {
	const A& a; const array<T>& b;
	struct { cat* c; operator int() const { return c->a.size+c->b.size; } } size;
	cat(const A& a,const array<T>& b) : a(a), b(b) { size.c=this; }
	void copy(T* data) const { a.copy(data); ::copy(data+a.size,b.data,b.size); }
	operator array<T>() { array<T> r; r.resize(size); copy((T*)r.data); return r; }
};
template <class A, class T> cat<A,T> operator +(const A& a,const array<T>& b) { return cat<A,T>(a,b); }

inline void log_(const string& s) { write(1,s.data,(size_t)s.size); }
