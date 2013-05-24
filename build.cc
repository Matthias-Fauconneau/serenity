#include "thread.h"

struct Build {
    Build() {
        string build = getenv("BUILD"_), target = getenv("TARGET"_);
        // Recursively scans all dependency files
        array<string> modules;
        array<string> stack = target;
        while(stack) {
            string module = stack.pop();
            if(modules.contains(module)) continue;
            modules << module;
            File
        }
    }
} build;
