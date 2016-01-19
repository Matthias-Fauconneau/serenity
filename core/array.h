#pragma once
/// \file array.h Contiguous collection of elements
#include "memory.h"
extern "C" void* realloc(void* buffer, size_t size) noexcept;

/// Managed variable capacity reference to an array of elements
/// \note Data is either an heap allocation managed by this object or a reference to memory managed by another object.
generic struct array : buffer<T> {
 using buffer<T>::data;
 using buffer<T>::size;
 using buffer<T>::capacity;

 using buffer<T>::end;
 using buffer<T>::at;
 using buffer<T>::last;
 using buffer<T>::slice;
 using buffer<T>::set;

 //using buffer<T>::buffer;
 array() {}
 /// Converts a buffer to an array
 array(buffer<T>&& o) : buffer<T>(::move(o)) {}
 /// Allocates an empty array with storage space for \a capacity elements
 explicit array(size_t capacity) { reserve(capacity); }

 // -- Variable capacity
 /// Allocates enough memory for \a capacity elements
 void reserve(size_t nextCapacity) {
  assert(nextCapacity>=size);
  if(nextCapacity>capacity) {
   nextCapacity = max(nextCapacity, capacity*2); // Amortizes reallocation
   if(capacity) {
    data = (T*)realloc((T*)data, nextCapacity*sizeof(T)); // Reallocates heap buffer (copy is done by allocator if necessary)
   } else {
    const T* data = 0;
    if(posix_memalign((void**)&data,16,nextCapacity*sizeof(T))) error("Out of memory"); // TODO: move compatible realloc
    swap(data, this->data);
    assert_(!size); //mref<T>::move(mref<T>((T*)data, size));
    if(capacity) free((void*)data);
   }
   capacity = nextCapacity;
  }
 }
 /// Sets the array size to \a size and destroys removed elements
 void shrink(size_t nextSize) { assert(capacity && nextSize<=size); for(size_t i: range(nextSize,size)) data[i].~T(); size=nextSize; }
 /// Removes all elements
 void clear() { if(size) shrink(0); }
 /// Grows the array to \a size without initializing new elements
 /// \return Previous size
 size_t grow(size_t size) { reserve(size); swap(this->size, size); return size; }

 // - Append
 /// Appends a default element
 T& append() { grow(size+1); return set(size-1); }
 /// Appends an implicitly copiable value
 T& append(const T& e) { grow(size+1); return set(size-1, e); }
 /// Appends a movable value
 T& append(T&& e) { grow(size+1); return set(size-1, ::move(e)); }
 /// Appends another list of elements to this array by copying
 bool append(const ref<T> source) { grow(size+source.size); slice(size-source.size).copy(source); return source.size; }
 /// Appends another list of elements to this array by moving
 bool append(const mref<T> source) { grow(size+source.size); slice(size-source.size).move(source); return source.size; }
 /// Appends a new element
 template<Type Arg0, Type Arg1, Type... Args> T& append(Arg0&& arg0, Arg1&& arg1, Args&&... args) {
  grow(size+1); return set(size-1, forward<Arg0>(arg0), forward<Arg1>(arg1), forward<Args>(args)...);
 }
 /// Appends the element, if it is not present already
 template<Type F> T& add(F&& e) { size_t i = ref<T>::indexOf(e); if(i!=invalid) return at(i); else return append(forward<F>(e)); }

 /// Inserts an element at \a index
 template<Type V> T& insertAt(size_t index, V&& e) {
  assert(int(index)>=0);
  grow(size+1);
  if(int(size)-2>=int(index)) {
   //set(size-1, ::move(at(size-2))); // Initializes last slot
   for(int i=size-2;i>=int(index);i--) set(i+1, ::move(at(i))); // Shifts elements
   //for(int i=size-3;i>=int(index);i--) at(i+1)= ::move(at(i)); // Shifts elements
   at(index) = ::move(e);
   return at(index);
  } else return set(index, ::move(e)); // index==size-1
 }
 /// Inserts immediately before the first element greater than the argument
 size_t insertSorted(T&& e) { size_t index=reverseLinearSearch(e); insertAt(index, ::move(e)); return index; }
 size_t insertSorted(const T& e) { size_t index=reverseLinearSearch(e); insertAt(index, ::copy(e)); return index; }

 /// Removes one element at \a index
 void removeAt(size_t index) { at(index).~T(); for(size_t i: range(index, size-1)) raw(at(i)).copy(raw(at(i+1))); size--; }
 /// Removes one element at \a index and returns its value
 T take(size_t index) { T value = move(at(index)); removeAt(index); return value; }
 /// Removes the last element and returns its value
 T pop() { return take(size-1); }

 /// Removes one matching element and returns an index to its successor, or invalid if none match
 template<Type K> size_t tryRemove(const K& key) { size_t i=ref<T>::indexOf(key); if(i!=invalid) removeAt(i); return i; }
 /// Removes one matching element and returns an index to its successor, aborts if none match
 template<Type K> size_t remove(const K& key) { size_t i=ref<T>::indexOf(key); removeAt(i); return i; }
 /// Filters elements matching predicate
 template<Type F> array& filter(F f) { for(size_t i=0; i<size;) if(f(at(i))) removeAt(i); else i++; return *this; }

 /// Returns index to the first element greater than \a value using linear search (assuming a sorted array)
 template<Type K> size_t linearSearch(const K& key) const { size_t i=0; while(i<size && at(i) <= key) i++; return i; }
 /// Returns index after the last element lesser than \a value using linear search (assuming a sorted array)
 template<Type K> size_t reverseLinearSearch(const K& key) const { size_t i=size; while(i>0 && at(i-1) > key) i--; return i; }
 /// Returns index to the first element greater than or equal to \a value using binary search (assuming a sorted array)
 template<Type K> size_t binarySearch(const K& key) const {
  size_t min=0, max=size;
  while(min<max) {
   size_t mid = (min+max)/2;
   assert(mid<max);
   if(at(mid) < key) min = mid+1;
   else max = mid;
  }
  assert(min == max);
  return min;
 }
};
/// Copies all elements in a new array
generic array<T> copy(const array<T>& o) { return copyRef(o); }
generic array<buffer<T> > copy(const array<buffer<T>>& o) {
 buffer<buffer<T>> copy(o.size);
 for(size_t index: range(o.size)) copy.set(index, copyRef(o[index]));
 return move(copy);
}

// -- Sort --

generic uint partition(const mref<T>& at, size_t left, size_t right, size_t pivotIndex) {
 swap(at[pivotIndex], at[right]);
 const T& pivot = at[right];
 uint storeIndex = left;
 for(uint i: range(left,right)) {
  if(pivot < at[i]) {
   swap(at[i], at[storeIndex]);
   storeIndex++;
  }
 }
 swap(at[storeIndex], at[right]);
 return storeIndex;
}

generic T quickselect(const mref<T>& at, size_t left, size_t right, size_t k) {
 for(;;) {
  size_t pivotIndex = partition(at, left, right, (left + right)/2);
  size_t pivotDist = pivotIndex - left + 1;
  if(pivotDist == k) return at[pivotIndex];
  else if(k < pivotDist) right = pivotIndex - 1;
  else { k -= pivotDist; left = pivotIndex + 1; }
 }
}
/// Quickselects the median in-place
generic T median(const mref<T>& at) { if(at.size==1) return at[0]; return quickselect(at, 0, at.size-1, at.size/2); }

generic void quicksort(const mref<T>& at, int left, int right) {
 if(left < right) { // If the list has 2 or more items
  int pivotIndex = partition(at, left, right, (left + right)/2);
  if(pivotIndex) quicksort(at, left, pivotIndex-1);
  quicksort(at, pivotIndex+1, right);
 }
}
/// Quicksorts the array in-place
generic const mref<T>& sort(const mref<T>& at) { if(at.size) quicksort(at, 0, at.size-1); return at; }
