#include "thread.h"

struct Dep {
    Dep() {
        string build = getenv("BUILD"_), target = getenv("TARGET"_);
        // Recursively scans all dependency files
        array<string> modules;
        array<string> stack = target;
        while(stack) {
            string module = target.pop();
            modules << module;
            // parse dependencies


            for( nextFile in parseDepFile(depFile)) {
                newDepFile = "";

                // if we have a source file, we need to link against it
                if( regSrc.match(nextFile) ) {
                    linkFiles.add(nextFile);
                    newDepFile = buildDir + "/" + regSuffix.sub(".d", nextFile);
                }

                // check whether a .cpp/.c/.cc file exist
                srcFile = findSourceFile(nextFile);
                if(srcFile != None) {
                    linkFiles.add(srcFile);
                    newDepFile = buildDir + "/" + regSuffix.sub(".d", srcFile);
                }

                // if the corresponding .d file exists as parameter, add it to the stack
                if( newDepFile and os.path.exists(newDepFile) ) {
                    if(!modules.contains(module)) depFileStack.add(newDepFile);
                }
            }
        }
        //
        // generate all necessary rules
        //

        // all includes of dependency files
        for( i in linkFiles) {
            i = regSuffix.sub(".d", i);
            print("-include " + buildDir + "/" + i);
            print();

            // dependencies for link file
            print(linkFile + ": \\");
            for( i in linkFiles) {
                i = regSuffix.sub(".d", i);
                print("\t" + buildDir + "/" + i + " \\");
                print();
            }
            // print out all files we need to link against
            print(ruleTarget + ": " + linkFile + " \\");
            for( i in linkFiles) {
                i = regSuffix.sub(".o", i);
                print("\t" + buildDir + "/" + i + " \\");
                print();
            }
        }
    }
} dep;
