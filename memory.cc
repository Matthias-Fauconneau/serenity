#include "string.h"

#include <cxxabi.h>
string demangle(const char* mangled) {
    char* demangle = abi::__cxa_demangle(mangled,0,0,0);
    string name = demangle?copy(strz(demangle)):strz(name);
    free((void*)demangle);
    return name;
}

static int count = 0;
static bool recurse=false;
void* allocate_(int size, const char* type) {
    count++;
    if(!recurse) {
        recurse=true;
        logTrace(3);
        log(count,"allocate"_,demangle(type),size);
        __builtin_abort();
        recurse=false;
    }
    return malloc(size);
}

void *reallocate_(void* buffer, int, int size) { return realloc(buffer,size); }
void unallocate_(void* buffer) { if(!recurse) { log(count,"unallocate"_); } count--; free(buffer); }
