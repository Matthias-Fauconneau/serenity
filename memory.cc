#include "string.h"
#include <stdlib.h>

#ifdef TRACE_MALLOC
#include <cxxabi.h>
string demangle(const char* mangled) {
    char* demangle = abi::__cxa_demangle(mangled,0,0,0);
    string name = demangle?copy(strz(demangle)):strz(name);
    free((void*)demangle);
    return name;
}
bool recurse=false;

void* allocate_(int size, const char* type) {
    if(!recurse) {
        recurse=true;
        logTrace(3);
        log("allocate"_,demangle(type),size);
        recurse=false;
    }
#else
byte* malloc(int size) {
#endif
    return malloc(size);
}

void *reallocate_(void* buffer, int, int size) { return realloc(buffer,size); }
void unallocate_(void* buffer) { free(buffer); }
