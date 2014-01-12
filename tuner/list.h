#pragma once
#include "core.h"

template<Type V, uint N> struct list { // Small sorted list
    static constexpr uint capacity = N;
    uint size = 0;
    struct element : V { float key; element(float key=0, V&& value=V()):V(move(value)),key(key){} } elements[N];
    void clear() { for(size_t i: range(size)) elements[i]=element(); size=0; }
    void insert(float key, V&& value) {
        uint i=0;
        while(i<size && elements[i].key<=key) i++;
        if(size<capacity) {
            for(int j=size-1; j>=(int)i; j--) elements[j+1]=move(elements[j]); // Shifts right
            size++;
            elements[i] = element(key, move(value)); // Inserts new candidate
        } else {
            if(i==0) return; // New candidate would be lower than current
            if(elements[i-1].key==key) return;
            for(uint j: range(i-1)) elements[j]=move(elements[j+1]); // Shifts left
            assert_(i>=1 && i<=size, i, size, N);
            elements[i-1] = element(key, move(value)); // Inserts new candidate
        }
    }
    void remove(V key) {
        for(uint i=0; i<size; i++) {
            if((V)elements[i] == key) {
                for(uint j: range(i, size-1)) elements[j]=move(elements[j+1]); // Shifts left
                size--;
                return;
            }
        }
    }
    const element& operator[](uint i) const { assert(i<size); return elements[i]; }
    const element& last() const { return elements[size-1]; }
    const element* begin() const { return elements; }
    const element* end() const { return elements+size; }
    ref<element> slice(size_t pos) const { assert(pos<=size); return ref<element>(elements+pos,size-pos); }
};
