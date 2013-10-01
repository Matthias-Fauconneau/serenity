#include "core.h"

// Header for a file-block (BHead in blender)
struct BlockHeader {
    char identifier[4];
    uint32 size; // Total length of the data after the block header
    uint64 address; // Base memory address used by pointers pointing in this block
    uint32 type; // Type of the stored structure (as an index in SDNA types)
    uint32 count; // Number of structures located in this block
};

struct ID {
    ID* next;
    ID* prev;
    ID* newid;
    struct Library* lib;
    char name[66];
    short pad;
    short us;
    short flag;
    int icon_id;
    int pad2;
    struct IDProperty* properties;
};

template<class T> struct ID_iterator {
    const ID* pointer;
    ID_iterator(const ID* pointer):pointer(pointer){}
    void operator++() { pointer = pointer->next; }
    bool operator!=(const ID_iterator<T>& b) const { return pointer != b.pointer; }
    const T& operator*() const { return (const T&)*pointer; }
};

template<class T=void> struct ListBase {
 const ID* first;
 const ID* last;
 const ID_iterator<T> begin() const { return first; }
 const ID_iterator<T> end() const { return 0; }
};
