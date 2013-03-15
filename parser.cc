#include "parser.h"
//#include "process.h"
#include "file.h"

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
            assert(token,str(s.peek()),s.untilEnd());
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
    while(s) {
        s.skip();
        if(s.match('#')) { s.line(); continue; }
        word name = s.until(':'); assert(name); if(!firstRule) firstRule=name;
        if(rules.contains(name))
            error("duplicate",rules[rules.indexOf(name)],"and", str(name)+":"_+s.line(),
                    " \t(use choice operator '|' instead of multiple rules)");
        for(;;) {
            Rule rule(name, parseRuleExpression(s));
            if(s.match('{')) {
                s.skip(); word name=s.word(); s.skip(); s.match(':'); s.skip();
                word action = s.word(); s.skip();
                Attribute attribute(name, actions.contains(action)?&actions.at(action):0);
                while(!s.match('}')) {
                    word token = s.word();
                    assert(token,s.untilEnd());
                    assert(rule.tokens.contains(token),"'"_+str(token)+"'"_,rule.tokens);
                    int index = rule.tokens.indexOf(token);
                    s.match('.');
                    word name = s.word();
                    assert(name,s.until('}'));
                    attribute.inputs << Attribute::Input{index,name};
                    s.skip();
                }
                rule.attributes << move(attribute);
                s.whileAny(" "_);
            }
            rules<< move(rule);
            if(!s.match('|')) { if(!s.match('\n')) error("Expected newline",s.untilEnd()); break; }
        }
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
    array<int> stack; stack<< 0;
    array<Node> nodeStack;
    constexpr bool verbose = false;
    for(int i=0;;) {
        if(!isTerminal(input[i])) { log("Invalid",input[i]); return input[i]; }
        const map<word, int>& transitions = states[stack.last()].transitions;
        if(!transitions.contains(input[i])) { log("Expected {",transitions.keys,"} at ",input.slice(0,i),"|",input.slice(i)); return input[i]; }
        int action = transitions.at(input[i]);
        if(action>0) {
            if(verbose) log(input[i],"shift",action,/*states[action].items,"\t",*/stack);
            stack << action;
            i++;
        }
        else if(action<0) {
            const Rule& rule=extended[-action];
            if(verbose) log(input[i],"reduce", -action, rule,"\t",stack);

            Node node = rule.symbol;
            array<Node> nonterminal;
            for(word token: rule.tokens) if(!isTerminal(token)) nonterminal<< nodeStack.pop();
            if(!rule.attributes && nonterminal.size==1) { // automatically forward attributes
                node.values = move(nonterminal.first().values);
            }
            for(word token: rule.tokens) {
                if(isTerminal(token)) node.children<< token;
                else node.children<< nonterminal.pop();
            }
            if(rule.attributes) {
                for(const Attribute& attribute: rule.attributes) {
                    array<unique<Value>> values;
                    if(attribute.inputs) {
                        for(Attribute::Input input: attribute.inputs) values<< node.children[input.index]->values.take(input.name); //synthesize
                        assert(attribute.action);
                        node.values.insert(attribute.name,attribute.action->invoke(values));
                    } else {
                        string token = str(node); //immediate (FIXME: slice input instead of flattening parse tree)
                        values << unique< ValueT< ref<byte> > >(token);
                        assert(attribute.action);
                        node.values.insert(attribute.name, attribute.action->invoke(values));
                    }
                }
            }

            nodeStack<< move(node);

            stack.shrink(stack.size-rule.size());
            if(verbose) log("goto",stack.last(),rule.symbol,states[stack.last()].transitions.at(rule.symbol));
            stack<< states[stack.last()].transitions.at(rule.symbol);
        } else break;
    }
    Node root = nodeStack.pop();
    //log(root);
    assert(!nodeStack);
    return root;
}
