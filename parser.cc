#include "parser.h"
#include "data.h"

array<string> pool;
static const word e = "ε"_;

array<word> Parser::parseRuleExpression(TextData& s) {
    array<word> tokens;
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
                    constexpr ref<byte> all = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ`~-=\\!@#$%^&*()_+|\t\n[]{}';\":/.,?><"_;
                    for(byte c: all) if(!notSet.contains(c)) set<< c;
                }
                for(byte c: set) {
                    array<word> tokens; tokens<<word(str((char)c)); rules<<Rule(name,move(tokens));
                }
            }
            tokens<< name;
        } else if(s.match('(')) { //nested rule
            int start=s.index-1;
            array<Rule> nested;
            for(;;) {
                nested<< Rule(""_, parseRuleExpression(s));
                if(!s.match('|')) { if(!s.match(')')) error(""); break; }
            }
            word name = s.slice(start, s.index-start);
            if(!rules.contains(name)) for(Rule& rule: nested) rules<< Rule(name,move(rule.tokens));
            tokens<< name;
        } else if(s.match('\'')) { //character token
            tokens<< str(s.character());
            if(!s.match('\'')) error("invalid character literal");
        } else if(s.match('"')) { //keyword
            int start=s.index-1;
            array<word> keyword;
            while(!s.match('"')) keyword<< str(s.character());
            word name = s.slice(start, s.index-start);
            Rule rule(name, move(keyword));
            if(!rules.contains(rule)) rules<< move(rule);
            tokens<< name;
        } else if(s.match('?')) { // T?: T |
            word T = tokens.pop();
            word Tp = string(str(T)+"?"_);
            if(!rules.contains(Tp)) rules<< Rule(Tp, T) << Rule(Tp, array<word>());
            tokens<< Tp;
        } else if(s.match('+')) { //T+: T | T+ T
            word T = tokens.pop();
            word Tp = string(str(T)+"+"_);
            if(!rules.contains(Tp)) rules<< Rule(Tp, T) << Rule(Tp, Tp, T);
            tokens<< Tp;
        } else if(s.match('*')) { //T*: T+?
            word T = tokens.pop();
            word Tp = string(str(T)+"+"_);
            if(!rules.contains(Tp)) rules<< Rule(Tp, T) << Rule(Tp, Tp, T);
            word Ts = string(str(T)+"*"_);
            if(!rules.contains(Ts)) rules<< Rule(Ts, Tp) << Rule(Tp);
            tokens<< Ts;
        } else { //nonterminal
            word token = s.identifier();
            if(token) { tokens<< token; used+=token; }
            if(!s||s.peek()=='|'||s.peek()==')'||s.peek()=='{'||s.peek()=='\n') break;
            assert(token,s.untilEnd());
        }
    }
    return tokens;
}

void Parser::computeItemSet(array<Item>& items, int index) {
    if(items[index].dot < items[index].size()) {
        int i=0; for(const Rule& rule: rules) {
            if(rule.symbol == items[index].expected()) {
                Item item(rules, i, 0);
                if(!items.contains(item)) {
                    items << item;
                    computeItemSet(items, items.size-1);
                }
            }
            i++;
        }
    }
}

void Parser::computeTransitions(int current, word token) {
    State next;
    for(Item item: states[current].items) {
        if(item.dot < item.size() && token == item.expected()) {
            item.dot++;
            next.items << item;
            computeItemSet(next.items,next.items.size-1);
        }
    }
    if(next.items) {
        int j = states.indexOf(next);
        if(j<0) { j=states.size; states << move(next); }
        states[current].transitions.insert(token, j);
    }
}

array<word> Parser::first(const word& X) {
    array<word> firstX;
    if(isTerminal(X)) firstX+= X;
    else for(const Rule& rule: extended) if(rule.extended == X) {
        if(rule.size()==0 || (rule.size()==1 && rule[0]==e)) firstX+= e;
        else firstX+= first(X, rule.tokens);
    }
    return firstX;
}

array<word> Parser::first(const word& X, const ref<word>& Y) {
    array<word> firstY;
    if(Y[0]!=X) firstY = first(Y[0]);
    if(firstY.contains(e)) {
        firstY.removeAll(e);
        if(firstY.size>1) firstY+= first(X, Y.slice(1));
        for(word y: Y) if(!first(y).contains(e)) { return firstY; }
        firstY<< e;
    }
    return firstY;
}

array<word> Parser::follow(const word& X) {
    array<word> followX;
    if(X=="0-1"_) followX+= "end"_;
    else if(isTerminal(X)) {}
    else for(const Rule& rule: extended) {
        for(uint i: range(rule.size())) {
            if(rule[i]!=X) continue;
            if(i+1<rule.size()) { //R → a*Xbc*: add First(b) to Follow(X)
                array<word> firstB = first(rule[i+1]);
                followX+= firstB;
                if(firstB.contains(e)) followX+= follow(rule.extended);
            } else { //R → a*X: add Follow(R) to Follow(X)
                followX+= follow(rule.extended);
            }
        }
    }
    return followX;
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
                    word token = s.word();
                    int index=-1;
                    if(s.match('[')) { index=s.integer(); s.match(']'); }
                    int found=-1; int count=0;
                    for(uint i: range(rule.tokens.size)) {
                        if(rule.tokens[i]!=token) continue;
                        if(index<0) {
                             if(count==0) found=i;
                             else error("Multiple match for",token,"in",rule.tokens);
                        } else {
                            if(count==index) found=i;
                        }
                        count++;
                    }
                    if(found<0) error("No match for '"_+str(token)+"' in {"_,rule.tokens,"}"_);
                    s.match('.');
                    word name = s.word();
                    if(attribute.inputs.contains(Attribute::Input{found,name})) error(str(token)+"."_+str(name),"used twice");
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
    //log(rules,'\n');
    rules.insertAt(0,Rule(""_,firstRule));
    for(const Rule& rule: rules) nonterminal+= rule.symbol;
    for(word use: used) if(!isNonTerminal(use)) error("Undefined rule for '"_+str(use)+"'"_);
    for(const Rule& rule: rules) for(word token: rule.tokens) if(!isNonTerminal( token)) terminal+= token;
    terminal << "end"_;

    /// Computes item sets and transitions
    State start; start.items<<Item(rules, 0, 0);
    computeItemSet(start.items,0);
    states << move(start);
    for(uint i=0; i<states.size; i++) { //no foreach as states is growing
        for(word token: terminal) computeTransitions(i,token);
        for(word token: nonterminal) computeTransitions(i,token);
    }

    if(0) { /// Display item sets
        int i=0; for(const State& state: states) { log("Item set",i,": ",str(state.items," | "_)); i++; }
    }
    if(0) { /// Display transitions
        log("\t"_+str(terminal,"\t"_)+"\t|\t"_+str(nonterminal.slice(1),"\t"_));
        int i=0; for(const State& state: states) {
            string s=str(i)+"\t"_;
            for(const word& key: terminal) { if(state.transitions.contains(key)) s << str(state.transitions.at(key)); s<<'\t'; }
            for(const word& key: nonterminal) { if(state.transitions.contains(key)) s << str(state.transitions.at(key)); s<<'\t'; }
            log(s);
            i++;
        }
    }

    /// Creates extended grammar
    int i=0; for(State& state: states) {
        for(Item& item: state.items) {
            if(item.dot==0) { // && item.size() ?
                array<word> tokens; int last=i;
                for(word token: item.rule().tokens) {
                    int t = states.at(last).transitions.at(token);
                    if(isTerminal(token)) tokens<< token;
                    else tokens<< word( str(last)+str(token)+str(t) );
                    last=t;
                }
                word symbol = item.rule().symbol;
                extended<< Rule(symbol, move(tokens), item.ruleIndex, last, i, (symbol==""_?-1:state.transitions.at(symbol)), move(item.rule().attributes));
            }
        }
        i++;
    }
    //rules = move(extended);

    /// Compute follow sets before merging
    //TODO: check if there are recursive rules which would fail
    for(uint i: range(1,extended.size)) { Rule& a = extended[i]; a.followSet = copy(follow(a.extended)); }

    /// Merges rules with same original rule and end state
    for(uint i=0; i<extended.size; i++) { const Rule& a = extended[i];
        for(uint j: range(i)) { Rule& b = extended[j];
            if(a.original == b.original && a.end==b.end) {
                b.followSet+= a.followSet; // merge follow sets
                extended.removeAt(i), i--;
                //for(auto& state: states) for(auto& item: state.items) if(item.ruleIndex>=i) item.ruleIndex--;
                break;
            }
        }
    }

    /// Adds LALR reductions using rules final state and follow set
    {int i=0; for(Rule& rule: extended) {
            for(word token: rule.followSet) {
                if(states[rule.end].transitions.contains(token)) {
                    int a = states[rule.end].transitions[token];
                    if(a<0) error("reduce/reduce conflict",token,rule,states[-a]);
                    else error("shift/reduce conflict '"_+str(token)+"'\n"_+str(rule)+"\n"_+str(states[rule.end]));
                }
                states[rule.end].transitions.insert(token, -i);
            }
            i++;
        }}

    /// Accepts when we have end of input on item : x* •
    for(State& set: states) if(set.items.contains(Item(extended, 0, extended[0].size()))) set.transitions["end"_] = 0;

    if(0) { /// Displays parsing table
        log("\t"_+str(terminal,"\t"_)+"        |\t"_+str(nonterminal.slice(1),"\t"_));
        {int i=0; for(const State& state: states) {
                string s=str(i)+"\t"_;
                for(const word& key: terminal) {
                    if(state.transitions.contains(key)) {
                        int t = state.transitions.at(key);
                        if(t>0) s<<'s'<<str(t);
                        if(t<0) s<<'r'<<str(-t);
                        if(t==0) s<<"acc"_;
                    }
                    s<<'\t';
                }
                for(const word& key: nonterminal.slice(1)) { if(state.transitions.contains(key)){ s<<"g"_<<str(state.transitions.at(key)); } s<<'\t'; }
                log(s);
                i++;
            }}
    }
}

Node Parser::parse(const ref<byte>& text) {
    /// LR parser
    array<word> input; for(char c: text) input<< str(c); input<<"end"_;
    array<int> stack; stack<< 0; // Parser stack
    array<int> inputStack; inputStack<< 0; // Input position stack
    array<Node> nodeStack; // Parse tree stack
    constexpr bool verbose = false;
    for(int i=0;;) {
        if(!isTerminal(input[i])) { log("Invalid token '"_+str(input[i])+"'"_); return input[i]; }
        const map<word, int>& transitions = states[stack.last()].transitions;
        if(!transitions.contains(input[i])) { log("Expected {",transitions.keys,"} at ",input.slice(0,i),"|",input.slice(i)); return input[i]; }
        int action = transitions.at(input[i]);
        if(action>0) {
            if(verbose) log(input[i],"shift",action,/*states[action].items,"\t",*/stack);
            stack << action;
            i++;
            inputStack << i;
        }
        else if(action<0) {
            const Rule& rule=extended[-action];
            if(verbose) log(input[i],"reduce", -action, rule,"\t",stack);
            Node node = rule.symbol;
            uint begin = inputStack[inputStack.size-1-rule.size()];
            node.input = text.slice(begin, i-begin);
            array<Node> nonterminal;
            for(word token: rule.tokens) if(!isTerminal(token)) nonterminal<< nodeStack.pop();
            if(!rule.attributes && nonterminal.size==1) { // automatically forward all attributes (TODO: partial forward)
                node.values = move(nonterminal.first().values);
            }
            for(word token: rule.tokens) {
                if(isTerminal(token)) node.children<< token;
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

            nodeStack<< move(node);

            stack.shrink(stack.size-rule.size());
            inputStack.shrink(inputStack.size-rule.size());
            if(verbose) log("goto",stack.last(),rule.symbol,states[stack.last()].transitions.at(rule.symbol));
            stack<< states[stack.last()].transitions.at(rule.symbol);
            inputStack<< i;
        } else break;
    }
    Node root = nodeStack.pop();
    //log(root);
    assert(!nodeStack);
    return root;
}
