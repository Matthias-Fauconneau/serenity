#pragma once

struct element {
 float key=inf; uint value=0;
 element() {}
 element(float key, uint value) : key(key), value(value) {}
};

String str(element e) { return "("+str(e.key)+", "+str(e.value)+")"; }

template<size_t N> struct list { // Small sorted list
 uint size = 0;
 element elements[N];
 bool insert(float key, uint value) {
  uint i = 0;
  for(;i<size && key>elements[i].key; i++) {}
  //if(i<size) assert(value != elements[i].value);
  if(size < N) size++;
  if(i < N) {
   for(uint j=size-1; j>i; j--) elements[j]=elements[j-1]; // Shifts right (drop last)
   assert(i < N, i, N);
   elements[i] = element(key, value); // Inserts new candidate
   //log(ref<element>(elements, size));
   return true;
  }
  else {
   //assert(!(key < elements[i-1].key), key, size, elements);
   return false;
  }
 }
};
