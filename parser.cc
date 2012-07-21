#include "parser.h"
#include "process.h"
#include "file.h"
#include "stream.h"
#include "array.cc"

array<string> unique;
const ref<byte> all = "12134567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ`~-=\\!@#$%^&*()_+|\t\n[]{}';\":/.,?><"_;
static const word e = "epsilon"_;

struct Parser : Application {
    array<Rule> rules;
    array<State> states;
    array<word> nonterminal, terminal;

    /// Computes item sets
    void computeItemSet(array<Item>& items, int index) {
        if(items[index].dot < items[index].size()) {
            int i=0; for(const Rule& rule: rules) {
                if(rule.symbol == items[index].expected()) {
                    Item item(rules, i, 0);
                    if(!contains(items, item)) {
                        items << item;
                        computeItemSet(items, items.size()-1);
                    }
                }
                i++;
            }
        }
    }

    /// Computes transitions
    void computeTransitions(int current, word token) {
        State next;
        for(Item item: states[current].items) {
            if(item.dot < item.rule().size() && token == item.expected()) {
                item.dot++;
                next.items << item;
                computeItemSet(next.items,next.items.size()-1);
            }
        }
        if(next.items) {
            int j = indexOf(states, next);
            if(j<0) { j=states.size(); states << move(next); }
            states[current].transitions.insert(token, j);
        }
    }

    /// Computes first set
    map<word, array<word> > firstSets;
    const array<word>& first(const word& X) {
        array<word>& firstX = firstSets[X];
        if(firstX) return firstX;
        if(contains(terminal,X)) firstX+= X;
        else for(const Rule& rule: rules) if(rule.extended == X) {
            if(rule.size()==0 || (rule.size()==1 && rule[0]==e)) error("empty"), firstX+= e;
            else firstX+= first(rule.tokens);
        }
        return firstX;
    }
    array<word> first(const ref<word>& Y) {
        array<word> firstY = copy(first(Y[0]));
        if(contains(firstY,e)) {
            removeAll(firstY, e);
            firstY+= first(slice(Y,1));
            for(word y: Y) if(!contains(first(y),e)) return firstY;
            firstY<< e;
        }
        return firstY;
    }

    /// Computes follow set
    array<word> follow(const word& X) {
        array<word> followX;
        if(X=="0S-1"_) followX+= "$"_;
        else if(contains(terminal,X)) {}
        else for(const Rule& rule: rules) {
            uint i = indexOf(rule.tokens, X);
            if(i==uint(-1)) continue;
            if(i+1<rule.size()) { //R → a*Xb: add First(b) to Follow(X)
                const array<word>& firstB = first(rule[i+1]);
                followX+= firstB;
                if(contains(firstB,e)) followX+= follow(rule.extended);
            }
            if(rule.tokens.last()==X) followX+= follow(rule.extended); //R → a*X: add Follow(R) to Follow(X)
        }
        return followX;
    }

    /// Parses rule expressions and generate rules from EBNF as needed
    array<word> parseRuleExpression(TextStream& s) {
        array<word> tokens;
        for(;;) {
            s.match(' ');
            if(s.match('[')) { //character set
                int start=s.index-1;
                array<byte> set;
                bool complement = s.match('^');
                while(!s.match(']')) {
                    if(s.match('-')) { byte first=set.pop(), last=s.character(); for(;first<=last;first++) set+= first; }
                    else set+= s.character();
                }
                word name = s.slice(start, s.index-start); assert(name);
                if(!contains(rules,Rule(name))) {
                    if(complement) {
                        array<byte> notSet = move(set);
                        for(byte c: all) if(!contains(notSet,c)) set<< c;
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
                if(!contains(rules,Rule(name))) for(Rule& rule: nested) rules<< Rule(name,move(rule.tokens));
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
                if(!contains(rules,rule)) rules<< move(rule);
                tokens<< name;
            } else if(s.match('+')) { //previous token may repeat T+: T | T+ T
                word T = tokens.pop();
                word Tp = string(str(T)+"+"_);
                if(!contains(rules,Rule(Tp))) rules<< Rule(Tp, i(array<word>{T})) << Rule(Tp, i(array<word>{Tp, T}));
                tokens<< Tp;
            } else { //nonterminal
                ref<byte> token = s.whileNo(" |)\n"_);
                if(token) tokens<< token; //FIXME: undefined are implicitly tokens
                if(!s.match(' ')) break;
            }
        }
        assert(tokens);
        return tokens;
    }

    Parser(array<string>&&) {
        /// Parses grammar
        TextStream s = readFile("serenity/math.g"_);
        ref<byte> text = s.until('\n');
        while(s) {
            if(s.match('#')) { s.until('\n'); continue; }
            word name = s.until(':'); assert(name);
            if(contains(rules,Rule(name)))
                error("duplicate",rules[indexOf(rules,Rule(name))],"and", str(name)+":"_+s.until('\n'),
                      " \t[use choice operator | instead of multiple rules]");
            for(;;) {
                Rule rule(name, parseRuleExpression(s));
                rules<< move(rule);
                if(!s.match('|')) { if(!s.match('\n')) error(""); break; }
            }
        }
        log(rules,"\n"_);
        for(const Rule& rule: rules) nonterminal+= rule.symbol;
        for(const Rule& rule: rules) for(word token: rule.tokens) if(!contains(nonterminal, token)) terminal+= token;
        terminal << "$"_;

        /// Computes item sets and transitions
        State start; start.items<<Item(rules, 0, 0);
        computeItemSet(start.items,0);
        states << move(start);
        for(uint i=0;i<states.size();i++) { //no foreach as states is growing
            for(word token: terminal) computeTransitions(i,token);
            for(word token: nonterminal) computeTransitions(i,token);
        }

        if(0) { /// Display item sets
            int i=0; for(const State& state: states) { log("Item set",i,": ",str(state.items," | "_)); i++; }
        }
        if(0) { /// Display transitions
            log("\t"_+str(terminal,"\t"_)+"\t|\t"_+str(slice(nonterminal,1),"\t"_));
            int i=0; for(const State& state: states) {
                string s=str(i)+"\t"_;
                for(const word& key: terminal) { if(state.transitions.contains(key)) s << str(state.transitions.at(key)); s<<'\t'; }
                for(const word& key: nonterminal) { if(state.transitions.contains(key)) s << str(state.transitions.at(key)); s<<'\t'; }
                log(s);
                i++;
            }
        }

        /// Creates extended grammar
        array<Rule> extended;
        int i=0; for(const State& state: states) {
            for(const Item& item: state.items) {
                if(item.dot==0) {
                    array<word> tokens; int last=i;
                    for(word token: item.rule().tokens) {
                        int t = states[last].transitions.at(token);
                        if(contains(terminal,token)) tokens<< token;
                        else tokens<< word( str(last)+str(token)+str(t) );
                        last=t;
                    }
                    word symbol = item.rule().symbol;
                    extended<< Rule(symbol, move(tokens), item.ruleIndex, last, i, (symbol=="S"_?-1:state.transitions.at(symbol)));
                }
            }
            i++;
        }
        rules = move(extended);

        /// Compute follow sets before merging
        //TODO: check if there are recursive rules which would fail
        for(uint i=1; i<rules.size(); i++) { Rule& a = rules[i]; a.followSet = copy(follow(a.extended)); }

        /// Merges rules with same original rule and end state
        for(uint i=0; i<rules.size(); i++) { Rule& a = rules[i];
            for(uint j=0; j<i; j++) { const Rule& b = rules[j];
                if(a.original == b.original && a.end==b.end) {
                    a.followSet+= b.followSet; // merge follow sets
                    rules.removeAt(j), i--;
                    break;
                }
            }
        }

        /// Adds LALR reductions using rules final state and follow set
        {int i=0; for(Rule& rule: rules) {
            for(word token: rule.followSet) {
                if(states[rule.end].transitions.contains(token)) error("reduce/reduce conflict",token,states[-states[rule.end].transitions[token]],rule);
                states[rule.end].transitions.insert(token, -i);
            }
            i++;
        }}

        /// Accepts when we have $ on item S: x* •
        for(State& set: states) if(contains(set.items, Item(rules, 0, rules[0].size()))) set.transitions["$"_] = 0;

        if(0) { /// Displays parsing table
            log("\t"_+str(terminal,"\t"_)+"        |\t"_+str(slice(nonterminal,1),"\t"_));
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
                    for(const word& key: slice(nonterminal,1)) { if(state.transitions.contains(key)){ s<<"g"_<<str(state.transitions.at(key)); } s<<'\t'; }
                    log(s);
                    i++;
                }}
        }

        /// LR parser
        array<word> input; for(char c: text) input<< str(c); input<<"$"_;
        array<int> stack; stack<< 0;
        array<int> output;
        const bool verbose=0;
        for(int i=0;;) {
            if(!contains(terminal,input[i])) { log("Invalid",input[i]); return; }
            const map<word, int>& transitions = states[stack.last()].transitions;
            if(!transitions.contains(input[i])) { log("Expected {",transitions.keys,"} at ",slice(input,0,i),"|",slice(input,i)); return; }
            int action = transitions.at(input[i]);
            if(action>0) {
                if(verbose) log(input[i],"shift",action,states[action].items,"\t",stack);
                stack << action;
                i++;
            }
            else if(action<0) {
                if(verbose) log(input[i],"reduce", -action, rules[-action],"\t",stack);
                output << -action;
                stack.shrink(stack.size() - rules[-action].size());
                if(verbose) log("goto",stack.last(),rules[-action].symbol,states[stack.last()].transitions.at(rules[-action].symbol));
                stack<< states[stack.last()].transitions.at(rules[-action].symbol);
            } else break;
        }

        /// Convert reductions to parse tree
        array<Node> nodeStack;
        for(int i: output) { const Rule& rule=rules[i];
            Node node = rule.symbol;
            array<Node> nonterminal;
            for(word token: rule.tokens) if(!contains(terminal, token)) nonterminal << nodeStack.pop();
            for(word token: rule.tokens) {
                if(contains(terminal, token)) node.children<< token;
                else node.children<<nonterminal.pop();
            }
            nodeStack<< move(node);
        }
        Node root = nodeStack.pop();
        log(root);
    }
};
Application(Parser)
