#include "process.h"
#include "file.h"
#include "map.h"
#include "string.h"
#include "array.cc"

static array<string> unique;
/// word is an index in a string table allowing fast move/copy/compare
struct word {
    int id;
    word(const string& s) { id=indexOf(unique, s); if(id<0) { id=unique.size(); unique<<copy(s); } }
    word(const ref<byte>& s):word(string(s)){}

};
bool operator ==(word a, word b) { return a.id == b.id; }
bool operator >(word a, word b) { return a.id > b.id; }
string str(const word& w) { return copy(unique[w.id]); }

struct Rule {
    word symbol;
    array<word> tokens;
    array<word> followSet;
    int original, from, to, end; // extended grammar
    Rule(const string& rule) : symbol(section(rule,':')) { for(const string& str: split(section(rule,':',1,-1))) tokens << str; }
    Rule(word symbol, int original, int from, int to):symbol(symbol),original(original),from(from),to(to){}
    word extended() const { return word(str(from)+str(symbol)+str(to)); }
    int size() const { return tokens.size(); }
    word operator []( int i ) const { return tokens[i]; }
};
string str(const Rule& r) { return str(r.extended())+" -> "_+str(r.tokens); }

struct Item {
    array<Rule>& rules;
    int ruleIndex;
    int dot;
    Item(array<Rule>& rules, int ruleIndex, uint dot):rules(rules),ruleIndex(ruleIndex),dot(dot){}
    const Rule& rule() const { return rules[ruleIndex]; }
    int size() { return rule().size(); }
    word operator []( int i ) { return rule()[i]; }
    word expected() const { assert(dot<rule().size()); return rule()[dot]; }
};
bool operator ==(const Item& a, const Item& b) { return a.ruleIndex==b.ruleIndex && a.dot == b.dot; }
string str(const Item& item) {
    return str(item.rule().extended())+" -> "_+str(slice(item.rule().tokens, 0, item.dot))+"  ."_+str(slice(item.rule().tokens, item.dot));
}

struct State {
    array<Item> items;
    map<word, int> transitions;
    State() { items.reserve(256); transitions.reserve(256); }
};
bool operator ==(const State& a, const State& b) { return a.items==b.items; }
string str(const State& state) { return str(state.items); }

struct Node { word name; array<Node> children; Node(word name):name(name){} };
string str(const Node& node) { return str(node.name)+str(node.children," "_,"()"_); }

static const word e = "e"_;

struct Parser : Application {
    array<Rule> rules;
    array<State> states;
    array<word> nonterminal, terminal;

    void computeItemSet(array<Item>& items, int index) {
        const Item& current = items[index];
        array<int> queue;
        if(current.dot < current.rule().size()) {
            int i=0; for(const Rule& rule: rules) {
                if(rule.symbol == current.expected()) {
                    Item item(rules, i, 0);
                    if(!contains(items, item)) {
                        queue << items.size();
                        items << item;
                    }
                }
            i++; }
        }
        while(queue) computeItemSet(items, queue.pop());
    }

    void computeTransitions(int current, word token) {
        State next;
        for(Item item: states[current].items) {
            if(item.dot < item.rule().size() && token == item.expected()) {
                item.dot++;
                next.items << item;
            }
        }
        if(next.items) {
            computeItemSet(next.items,0);
            int j = indexOf(states, next);
            if(j<0) { j=states.size(); states << move(next); }
            states[current].transitions.insert(token, j);
        }
    }

    array<word> first(const word& X) {
        array<word> firstX;
        if(contains(terminal,X)) firstX+= X;
        else for(const Rule& rule: rules) if(rule.extended() == X) {
            if(rule.size()==0 || (rule.size()==1 && rule[0]==e)) error("empty"), firstX+= e;
            else firstX+= first(rule.tokens);
        }
        return firstX;
    }
    array<word> first(const array<word>& Y) {
        array<word> firstY = first(Y[0]);
        if(contains(firstY,e)) {
            removeAll(firstY, e);
            firstY+= first(slice(Y,1));
            for(word y: Y) if(!contains(first(y),e)) return firstY;
            firstY<< e;
        }
        return firstY;
    }

    array<word> follow(const word& X) {
        array<word> followX;
        if(X=="0S-1"_) followX+= "$"_;
        else if(contains(terminal,X)) {}
        else for(const Rule& rule: rules) {
            int i = indexOf(rule.tokens, X);
            if(i<0) continue;
            if(i+1<rule.size()) { //R → a*Xb: add First(b) to Follow(X)
                auto firstB = first(rule[i+1]);
                followX+= firstB;
                if(contains(firstB,e)) followX+= follow(rule.extended());
            }
            if(rule.tokens.last()==X) followX+= follow(rule.extended()); //R → a*X: add Follow(R) to Follow(X)
        }
        return followX;
    }


    Parser(array<string>&&) {
        /// Parses grammar
        array<string> lines = split(readFile("serenity/math.g"_),'\n');
        string text = move(lines[0]);
        for(const string& rule: slice(lines,1)) if(!startsWith(rule,"#"_)) rules<< Rule(rule);
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

        /*/// Display item sets and transitions
        {int i=0; for(const State& state: states) { log("Item set",i,": ",str(state.items," | "_)); i++; }}
        log("\t"_+str(terminal,"\t"_)+"\t|\t"_+str(slice(nonterminal,1),"\t"_));
        {int i=0; for(const State& state: states) {
            string s=str(i)+"\t"_;
            for(const word& key: terminal) { if(state.transitions.contains(key)) s << str(state.transitions.at(key)); s<<'\t'; }
            for(const word& key: nonterminal) { if(state.transitions.contains(key)) s << str(state.transitions.at(key)); s<<'\t'; }
            log(s);
            i++;
        }}*/

        /// Creates extended grammar
        array<Rule> extended;
        int i=0; for(const State& state: states) {
            for(const Item& item: state.items) {
                if(item.dot==0) {
                    word symbol = item.rule().symbol;
                    Rule rule(symbol, item.ruleIndex, i, (symbol=="S"_?-1:state.transitions.at(symbol)));
                    int last=i;
                    for(word token: item.rule().tokens) {
                        int t = states[last].transitions.at(token);
                        if(contains(terminal,token)) rule.tokens<< token;
                        else rule.tokens<< word( str(last)+str(token)+str(t) );
                        last=t;
                    }
                    rule.end = last;
                    extended << move(rule);
                }
            }
            i++;
        }
        rules = move(extended);

        /// Compute follow sets before merging
        for(uint i=1; i<rules.size(); i++) { Rule& a = rules[i]; a.followSet = follow(a.extended()); }

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
            for(word token: rule.followSet) states[rule.end].transitions.insert(token, -i);
            i++;
        }}

        /// Accepts when we have $ on item S: x* •
        for(State& set: states) if(contains(set.items, Item(rules, 0, rules[0].size()))) set.transitions["$"_] = 0;

        /// Displays parsing table
        /*log("\t"_+str(terminal,"\t"_)+"        |\t"_+str(slice(nonterminal,1),"\t"_));
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
        }}*/

        /// LR parser
        array<word> input; for(char c: text) input<< str(c); input<<"$"_;
        array<int> stack; stack<< 0;
        array<int> output;
        for(int i=0;;) {
            if(!contains(terminal,input[i])) { log("Invalid",input[i]); return; }
            const map<word, int>& transitions = states[stack.last()].transitions;
            if(!transitions.contains(input[i])) { log("Expected {",transitions.keys,"} at '",slice(input,0,i),"|",slice(input,i)); return; }
            int action = transitions.at(input[i]);
            if(action>0) { //log(input[i],"shift",action,states[action].items,"\t",stack);
                i++;
                stack << action;
            }
            else if(action<0) { //log(input[i],"reduce", -action, rules[-action],"\t",stack);
                output << -action;
                stack.shrink(stack.size() - rules[-action].size());
                //log("goto",stack.last(),rules[-action].symbol,states[stack.last()].transitions.at(rules[-action].symbol));
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
        //if(text=="x=*x"_) assert(str(root)=="N(V(x) = E(V(* E(V(x)))))"_);
    }
};
Application(Parser)
