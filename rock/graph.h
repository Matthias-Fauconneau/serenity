#pragma once
#include "process.h"

struct GraphProcess : virtual Process {
    String id(const string& target, const Dict& arguments) {
        const Dict& localArguments = this->localArguments(target, arguments);
        return hex(ptr(&ruleForOutput(target)))+(localArguments?" "_+str(localArguments).slice(1,-1):String());
    }
    String label(const string& target, const Dict& arguments) {
        const Dict& localArguments = this->localArguments(target, arguments);
        return ruleForOutput(target).operation+(localArguments?" "_+str(localArguments).slice(1,-1):String());
    }
    String dot(array<String>& once, const Dict& arguments, const string& target) {
        String s;
        const Rule& rule = ruleForOutput(target);
        String targetResult = id(target, arguments);
        if(!once.contains(targetResult)) {
            once << copy(targetResult);
            if(&rule) {
                s <<'"'<<targetResult<<'"'<< "[shape=record, label=\""_<<label(target, arguments);
                for(string output: rule.outputs) s<<"|<"_<<output<<"> "_<<output;
                s<<"\"];\n"_;
                for(string input: rule.inputs) {
                    String inputResult = id(input, arguments);
                    s<<'"'<<inputResult<<"\":\""_<<input<<"\" -> \""_<<targetResult<<"\"\n"_;
                    s<<dot(once, arguments, input);
                };
            }
        }
        return s;
    }

    void generateSVG(const ref<string>& targets, const string& name, const string& folder){
        array<String> once;
        String s ("digraph \""_+name+"\" {\n"_);
        for(const string& target: targets) s << dot(once, arguments, target);
        s << "}"_;
        String path = "/dev/shm/"_+name+".dot"_;
        writeFile(path, s);
        ::execute("/ptmp/bin/dot"_,{path,"-Tsvg"_,"-o"_+folder+"/"_+name+".svg"_});
    }
};
