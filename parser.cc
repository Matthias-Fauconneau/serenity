#include "parser.h"
#include "data.h"

array<string> pool;
static const word e = "ε"_;

array<word> Parser::parseRuleExpression(TextData& s) {
    array<word> symbols;
    for(;;) {
        while(s.available(1) && s.match(' ')) {}
        if(s.match('[')) { //character set
            int start=s.index-1;
            array<byte> set;
            bool complement = s.match('^');
            while(!s.match(']')) {
                if(s.match('-')) { byte first=set.pop(), last=s.character(); for(;first<=last;first++) set+= first; }
                else if(s.match("\\-"_)) set+= byte('-');
                else set+= s.character();
            }
            word name = s.slice(start, s.index-start); assert(name);
            if(!rules.contains(name)) {
                if(complement) {
                    array<byte> notSet = move(set);
                    static constexpr ref<byte> all =
                            "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ`~-=\\!@#$%^&*()_+|\t\n[]{}';\":/.,?><"_;
                    for(byte c: all) if(!notSet.contains(c)) set<< c;
                }
                for(byte c: set) {
                    array<word> symbols; symbols<<word(str((char)c)); rules<<Rule(name,move(symbols));
                }
            }
            symbols<< name;
        } else if(s.match('(')) { //nested rule
            int start=s.index-1;
            array<Rule> nested;
            for(;;) {
                nested<< Rule(""_, parseRuleExpression(s));
                if(!s.match('|')) { if(!s.match(')')) error(""); break; }
            }
            word name = s.slice(start, s.index-start);
            if(!rules.contains(name)) for(Rule& rule: nested) rules<< Rule(name,move(rule.symbols));
            symbols<< name;
        } else if(s.match('\'')) { //character
            symbols<< str(s.character());
            if(!s.match('\'')) error("invalid character literal");
        } else if(s.match('"')) { //keyword
            int start=s.index-1;
            array<word> keyword;
            while(!s.match('"')) keyword<< str(s.character());
            word name = s.slice(start, s.index-start);
            Rule rule(name, move(keyword));
            if(!rules.contains(rule)) rules<< move(rule);
            symbols<< name;
        } else if(s.match('?')) { // T?: T |
            word T = symbols.pop();
            word Tp = string(str(T)+"?"_);
            if(!rules.contains(Tp)) rules<< Rule(Tp, T) << Rule(Tp, array<word>());
            symbols<< Tp;
        } else if(s.match('+')) { //T+: T | T+ T
            word T = symbols.pop();
            word Tp = string(str(T)+"+"_);
            if(!rules.contains(Tp)) rules<< Rule(Tp, T) << Rule(Tp, Tp, T);
            symbols<< Tp;
        } else if(s.match('*')) { //T*: T+?
            word T = symbols.pop();
            word Tp = string(str(T)+"+"_);
            if(!rules.contains(Tp)) rules<< Rule(Tp, T) << Rule(Tp, Tp, T);
            word Ts = string(str(T)+"*"_);
            if(!rules.contains(Ts)) rules<< Rule(Ts, Tp) << Rule(Tp);
            symbols<< Ts;
        } else { //nonterminal
            word symbol = s.identifier();
            if(symbol) { symbols<< symbol; used+=symbol; }
            if(!s||s.peek()=='|'||s.peek()==')'||s.peek()=='{'||s.peek()=='\n') break;
            assert(symbol,s.untilEnd());
        }
    }
    return symbols;
}

void Parser::generate(const ref<byte>& grammar) {
    /// Parses grammar
    TextData s(grammar);
    word firstRule=""_;
    s.skip();
    while(s) {
        if(s.match('#') || s.match("//"_)) { s.line(); s.skip(); continue; }
        word name = s.until(':'); assert(name,"Missing rule name"); if(!firstRule) firstRule=name;
        if(rules.contains(name))
            error("duplicate",rules[rules.indexOf(name)],"and", str(name)+":"_+s.line(),
                    " \t(use choice operator '|' instead of multiple rules)");
        for(;;) {
            Rule rule(name, parseRuleExpression(s));
            while(s.match('{')) {
                s.skip(); word name=s.word(); s.skip(); s.match(':'); s.skip();
                int save=s.index;
                word action = s.word();
                Attribute attribute(name, 0);
                if(s.match('.')) s.index=save;
                else {
                    if(!actions.contains(action)) { error("Unknown action",action); return; }
                    attribute.action = &actions.at(action);
                    s.skip();
                }
                while(!s.match('}')) {
                    word symbol = s.word();
                    int index=-1;
                    if(s.match('[')) { index=s.integer(); s.match(']'); }
                    int found=-1; int count=0;
                    for(uint i: range(rule.symbols.size)) {
                        if(rule.symbols[i]!=symbol) continue;
                        if(index<0) {
                             if(count==0) found=i;
                             else error("Multiple match for",symbol,"in",rule.symbols);
                        } else {
                            if(count==index) found=i;
                        }
                        count++;
                    }
                    if(found<0) error("No match for '"_+str(symbol)+"' in {"_,rule.symbols,"}"_);
                    s.match('.');
                    word name = s.word();
                    if(attribute.inputs.contains(Attribute::Input{found,name})) error(str(symbol)+"."_+str(name),"used twice");
                    attribute.inputs << Attribute::Input{found,name};
                    s.skip();
                }
                rule.attributes << move(attribute);
                s.whileAny(" "_);
            }
            rules<< move(rule);
            if(s.match('|')) continue;
            else if(s.match('\n')) { s.skip(); if(s.match('|')) continue; else break; }
            else if(s) { error("Expected newline",s.untilEnd()); break; }
        }
        s.skip();
    }
    log(rules,"\n"_);
    rules.insertAt(0,Rule(""_,firstRule));
    for(const Rule& rule: rules) nonterminal+= rule.symbol;
    for(word use: used) if(!isNonTerminal(use)) error("Undefined rule for '"_+str(use)+"'"_);
    for(const Rule& rule: rules) for(word symbol: rule.symbols) if(!isNonTerminal( symbol)) terminal+= symbol;
}


/*void Parser::backtrack(uint i) {
    for(const State& state: states[i]) {
        if(state.current == state.rule.size()) { // Completed edge
            nodes[state.origin].children << &nodes[i];
            backtrack(state.origin);
        }
    }
}*/

Node Parser::parse(const ref<byte>& input) {
    states.clear(); states.grow(input.size+1);
    states.first() << State{rules.first(),0,0};
    for(uint i: range(input.size+1)) {
        if(!states[i]) return Node(""_);
        word token = i<input.size?str(input[i]):""_;
        for(uint k=0; k<states[i].size; k++) {
            const State& A = states[i][k];
            const Rule& rule = A.rule;
            if(A.current == rule.size()) { // Complete (Match a nonterminal)
                log("complete", A);
                word symbol = A.rule.symbol;

                for(const State& B: states[A.origin]) {
                    if(B.next() == symbol) states[i] += State{B.rule, B.current+1, B.origin};
                }
            } else if(isNonTerminal(A.next())) { // Predict (Adds all new possible rules)
                log("predict", A);
                word next = A.next();
                for(const Rule& rule: rules) if(rule.symbol==next) states[i] += State{rule, 0, i};
            } else if(A.next()==token) {
                log("scan", A);
                states[i+1] += State{A.rule, A.current+1, A.origin}; // Scan (Match a terminal)
            }
        }
        log(str(i)+":\t"_, states[i], "\t"_);
    }
    //nodes.clear(); nodes.grow(input.size+1);
    //backtrack(input.size); // Backtrack completed edges to generate a parse tree
    //TODO: node names
    log(nodes.first());
    if(!nodes.first().children) error("Invalid input");
    return move(nodes.first());
}

//    array<Node> stack; stack<<Node("root"_); // Parse tree stack
/*
// Generates parse tree with attributes synthesized by semantic actions
Node node = symbol;
node.input = input.slice(A.origin, i-A.origin);
array<Node> nonterminal;
for(word symbol: rule.symbols) if(isNonTerminal(symbol)) nonterminal<< stack.pop();
if(!rule.attributes && nonterminal.size==1) { // automatically forward all attributes (TODO: partial forward)
    node.values = move(nonterminal.first().values);
}
for(word symbol: rule.symbols) {
    if(isTerminal(symbol)) node.children<< symbol;
    else node.children<< nonterminal.pop();
}
if(rule.attributes) {
    for(const Attribute& attribute: rule.attributes) {
        if(attribute.inputs) {
            if(attribute.action) {
                array<ValueT<ref<byte>>> literals; //need to stay allocated
                array<const Value*> values;
                for(const Attribute::Input& input: attribute.inputs) {
                    if(input.name) {
                        if(!node.children[input.index]->values.contains(input.name))
                            error("Missing attribute '"_+str(input.name)+"' in"_,node.children[input.index],"for",rule);
                        Value* value = &node.children[input.index]->values.at(input.name);
                        if(!value) error("Attribute was already forwarded, declare forwarding action last");
                        values<< value;
                    } else { //literal attribute
                        literals << copy(node.children[input.index]->input);
                        values << &literals.last();
                    }
                }
                node.values.insert(attribute.name,attribute.action->invoke(values));
            }
            else {
                if(attribute.inputs.size!=1) error("Invalid forwarding");
                const Attribute::Input& input = attribute.inputs[0];
                node.values.insert(attribute.name, node.children[input.index]->values.take(input.name)); //move
            }
        } else { //No arguments (literal attribute)
            ValueT<ref<byte>> value = copy(node.input);
            array<const Value*> values; values<<&value;
            assert(attribute.action, "No action handling attributes for",rule, attribute);
            node.values.insert(attribute.name, attribute.action->invoke(values));
        }
    }
}
stack<< move(node);
*/
