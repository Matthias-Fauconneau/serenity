#include "linux.h"
#include "debug.h"
int main() { extern void setupHeap(); setupHeap(); log("Hello World!"_); exit(0); }
