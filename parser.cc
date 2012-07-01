#include "process.h"
#include "file.h"
#include "map.h"
#include "string.h"
#include "array.cc"

static array<string> unique;
struct word {
    int id;
    word(const string& str) { string s=trim(str); id=indexOf(unique, s); if(id<0) { id=unique.size(); unique<<move(s); } }
    word(string&& s) { id=indexOf(unique, s); if(id<0) { id=unique.size(); unique<<move(s); } }
};
bool operator ==(word a, word b) { return a.id == b.id; }
bool operator >(word a, word b) { return a.id > b.id; }
string str(const word& w) { return copy(unique[w.id]); }

struct Rule {
    word symbol;
    array<word> tokens;
    Rule(const string& rule) : symbol(section(rule,':')) {
        for(const string& str: split(section(rule,':',1,-1))) tokens << str;
    }
    word operator []( int i ) const { return tokens[i]; }
    bool operator ==(const Rule& o) const { return symbol == o.symbol && tokens == o.tokens; }
};
string str(const Rule& r) { return str(r.symbol)+" -> "_+str(r.tokens); }

struct Item {
    const Rule& rule;
    uint dot;
    //word lookahead;
    //word operator []( int i ) { return rule.tokens[i]; }
    word expected() const { return rule[dot]; }
    bool operator ==(const Item& o) const { return rule==o.rule && dot == o.dot; }
};
string str(const Item& item) {
    return str(item.rule.symbol)+" -> "_+str(slice(item.rule.tokens, 0, item.dot))+"  ."_+str(slice(item.rule.tokens, item.dot));
}

struct State {
    array<Item> items;
    map<word, int> transitions;
    bool operator ==(const State& o) const { return items==o.items; }
};
string str(const State& state) { return str(state.items); }

static const word e = "e"_;

struct Parser : Application {
    array<Rule> rules; //aka productions
    array<State> states; //aka item sets
    array<word> nonterminal, terminal;
    map< word, array<word> > firstSet;
    map< word, array<word> > followSet;

    //transitive closure of direct read relation
    void close(array<Item>& items) { close(items,items[0]); }
    void close(array<Item>& items, Item current) {
        if(current.dot < current.rule.tokens.size()) {
            for(const Rule& rule: rules) if(rule.symbol == current.expected()) {
                Item item{rule, 0};
                if(!contains(items, item)) {
                    items << item;
                    close(items, item);
                }
            }
        }
    }

    array<word> first(const array<word>& Y) { //First(Y1Y2..Yk) is either
        array<word> firstY = first( Y[0] );
        if(contains(firstY,e)) firstY = first(Y[0]), removeAll(firstY, e), firstY<< first(slice(Y,1)); //OR everything in First(Y1) <except for e > ~ First(Y2..Yk)
        //If First(Y1) First(Y2)..First(Yk) all contain e then add e to First(Y1Y2..Yk) as well.
        bool allE=true; for(word y: Y) if( !contains(first(y),e) ) { allE=false; break; }
        if( allE ) firstY<< e;
        return firstY;
    }
    array<word> first(const word& X) {
        if(contains(terminal,X) ) { return i({X}); } //If X is a terminal then First(X) is just X!
        if( !firstSet.contains(X) ) { //If there is a Production X->
            for(const Rule& rule: rules) if(rule.symbol == X) {
                if( rule.tokens==array<word>{e} ) firstSet[X] << e; //If there is a Production X →` e then add e to first(X)
                else firstSet[X]<< first( rule.tokens ); //If there is a Production X → Y1`Y2..Yk then add first(Y1Y2..Yk) to first(X)
            }
         }
        return copy(firstSet[X]);
    }

    void computeTransitions(int current) {
        map<word, State> next;
        for(const Item& item: states[current].items) if( item.dot < item.rule.tokens.size() ) next[ item.expected() ].items << item;
        for(auto i: next) { word x = i.key; State& set = i.value;
            for(Item& item: set.items) item.dot++;
            if(set.items[0].dot < set.items[0].rule.tokens.size()) close(set.items);
            int j = indexOf(states, set);
            if(j<0) { j=states.size(); states << move(set); computeTransitions(j); }
            states[current].transitions[x] = j;
        }
    }

    Parser(array<string>&&) {
        rules << Rule("S: E"_);
        for(const string& rule: split((string)mapFile("serenity/lr0.g"_),'\n')) rules << Rule(rule);
        array<word> input; input<<word("1"_)<<word("+"_)<<word("1"_)<<word("$"_);
        //log(rules);
        for(const Rule& rule: rules) appendOnce(nonterminal, rule.symbol);
        for(const Rule& rule: rules) for(word token: rule.tokens) if(!contains(nonterminal, token)) appendOnce(terminal, token);
        terminal << word("$"_);
        //log("nonterminal:",nonterminal,"terminal:",terminal);

        State start; start.items<<Item{rules[0], 0};
        states << move(start);
        close(states[0].items);
        computeTransitions(0);

        // display item sets
        /*{int i=0; for(const auto& state: states) {
            log("Item set:",i,str(state.items," | "_));
            i++;
        }}*/

        for(State& set: states) {
            if(contains(set.items, Item{rules[0], 1})) set.transitions["$"_] = 0;
            for(const Item& item: set.items) {
                int m = indexOf(rules, item.rule);
                if(m==0) continue;
                if(item.dot == item.rule.tokens.size()) { //fill state with reduce
                    for(word t: terminal) {
                        if(set.transitions.contains(t)) error("Conflict",set.transitions[t],item.rule);
                        set.transitions[t] = -m;
                    }
                }
            }
        }

        // display parsing table
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

        //parse
        array<State*> stack; stack<<&states[0];
        array<Rule*> output;
        for(int i=0;;) {
            //log("Stack:\n",stack,"Input:\n",input.slice(0,i),"•",input.slice(i));
            int action = stack.last()->transitions.at(input[i]);
            if(action>0) { //log("shift",action);
                i++;
                stack << &states[action];
            }
            else if(action<0) { //log("reduce",-action);
                output << &rules[-action];
                log(rules[-action]);
                stack.shrink(stack.size() - rules[-action].tokens.size());
                stack << &states[stack.last()->transitions[rules[-action].symbol]];
            } else break;
        }
    }
};
Application(Parser)
