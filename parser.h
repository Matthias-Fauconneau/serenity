#pragma once
#include "map.h"
#include "string.h"
#include "memory.h"
#include "data.h"
#include "function.h"

/// word is an index in a string table allowing fast move/copy/compare
extern array<string> pool;
struct word {
    int id;
    word(const string& s) { id=pool.indexOf(s); if(id<0) { id=pool.size(); pool<<copy(s); } }
    word(const ref<byte>& s):word(string(s)){}
    explicit operator bool() const { return pool[id].size(); }
};
inline bool operator ==(word a, word b) { return a.id == b.id; }
inline const string& str(const word& w) { return pool[w.id]; }

struct Value {
    virtual string str() const = 0;
};
inline string str(const Value& value) { return value.str(); }
template<class T> struct ValueT : Value {
    T value;
    ValueT(T value):value(value){}
    operator T() const { return value; }
    string str() const override { return string(::str(value)); }
};

struct Action { virtual unique<Value> invoke(ref< unique<Value> >) const=0; };
template<class T, class... Params> struct ActionFunction;
template<class T, class... Params> struct ActionFunction<T(Params...)> : Action {
    function<T(Params...)> f;
    ActionFunction(function<T(Params...)> f):f(f){}

    /// Unpacks ref<unique<Value>>
    template<class P, class... R, class... Args> unique<Value> invoke(ref< unique<Value> > s, Args... args) const {
        return invoke<R...>(s.slice(1), args ___, *(const ValueT<P>*)&s[0]);
    }
    template<class... Args> unique<Value> invoke(ref< unique<Value> > unused s, Args... args) const {
        assert(s.size==0);
        return (unique< ValueT<T> >)f(args ___);
    }
    unique<Value> invoke(ref< unique<Value> > s) const override { return invoke<Params...>(s); }
};

struct Attribute {
    Attribute(word name, Action* action):name(name),action(action){}
    /// Attribute name
    word name;
    /// Action to synthesize this attribute
    Action* action;
    /// Input attributes used to generate this attribute
    struct Input { int index; word name; };
    array<Input> inputs;
};
inline string str(const Attribute::Input& v) { return str(v.index,v.name); }
inline string str(const Attribute& v) { return str(v.name,"= action("_+str(v.inputs)+")"_); }

struct Rule {
    const word symbol;
    array<word> tokens;
    const int original=0, end=0, from=0, to=0; const word extended=""_;
    array<word> followSet;
    array<Attribute> attributes;
    Rule(word symbol):symbol(symbol){}
    Rule(word symbol, word A):symbol(symbol){tokens<<A;}
    Rule(word symbol, word A, word B):symbol(symbol){tokens<<A<<B;}
    Rule(word symbol, array<word>&& tokens):symbol(symbol),tokens(move(tokens)){}
    Rule(word symbol, array<word>&& tokens, int original, int end, int from, int to, array<Attribute>&& attributes):
        symbol(symbol),tokens(move(tokens)),original(original),end(end),from(from),to(to),extended(str(from)+str(symbol)+str(to)),attributes(move(attributes)){}
    uint size() const { return tokens.size(); }
    word operator []( int i ) const { return tokens[i]; }
};
inline string str(const Rule& r) { return str(r.extended?:r.symbol)+" -> "_+str(r.tokens); }
inline bool operator==(const Rule& a, const Rule& b) { return a.symbol==b.symbol; }

struct Item {
    array<Rule>& rules;
    uint ruleIndex;
    uint dot;
    Item(array<Rule>& rules, uint ruleIndex, uint dot):rules(rules),ruleIndex(ruleIndex),dot(dot){ assert(dot<=size()); }
    const Rule& rule() const { return rules[ruleIndex]; }
    Rule& rule() { return rules[ruleIndex]; }
    uint size() const { return rule().size(); }
    word operator []( int i ) { return rule()[i]; }
    word expected() const { assert(dot<rule().size()); return rule()[dot]; }
};
inline bool operator ==(const Item& a, const Item& b) { return a.ruleIndex==b.ruleIndex && a.dot == b.dot; }
inline string str(const Item& item) {
    assert(item.ruleIndex<item.rules.size(),item.ruleIndex,item.rules.size());
    assert(item.dot<=item.size(),item.dot,item.size(),item.rule());
    return str(item.rule().symbol)+" -> "_+str(item.rule().tokens.slice(0, item.dot))+"  ."_+str(item.rule().tokens.slice(item.dot));
}

struct State {
    array<Item> items;
    map<word, int> transitions;
};
inline bool operator ==(const State& a, const State& b) { return a.items==b.items; }
inline string str(const State& state) { return " "_+str(state.items,"\n+"_); }

struct Node {
    word name;
    array<unique<Node> > children;
    map<word, unique<Value> > values;
    Node(word name):name(name){}
};
inline string str(const Node& node) {
    if(!node.values && node.children.size()==1) return str(node.children[0]);
    if(!node.values && node.children.size()==2 && node.children[0]->name==node.name) return str(node.children[0])+str(node.children[1]);
    return str(node.name)+(node.values?"["_+str(node.values)+"]"_:string())+(node.children?"("_+str(node.children)+")"_:string());
}

struct Parser {
    const word e = "epsilon"_;
    array<Rule> rules;
    array<Rule> extended;
    array<State> states;
    array<word> nonterminal, terminal, used;

    bool isTerminal(word w) { return terminal.contains(w); }
    bool isNonTerminal(word w) { return nonterminal.contains(w); }
    /// Parses rule expressions and generate rules from EBNF as needed
    array<word> parseRuleExpression(TextData& s);
    /// Computes item sets
    void computeItemSet(array<Item>& items, int index);
    /// Computes transitions
    void computeTransitions(int current, word token);
    /// Computes first set
    map<word, array<word> > firstSets;
    array<word> first(const word& X);
    array<word> first(const word& X, const ref<word>& Y);
    /// Computes follow set
    array<word> follow(const word& X);

    map<word, unique<Action> > actions;
    struct ActionRef {
        Parser* parser;
        word name;
        ActionRef(Parser* parser, word name):parser(parser),name(name){}
        /// Converts a function value to an ActionFunction reference
        template<class T, class... Args> void operator=(function<T(Args...)> func) {
            parser->actions.insert(word(name), unique<ActionFunction<T(Args ___)>>(func) );
        }
    };
    ActionRef operator[](const ref<byte>& name) { return ActionRef(this,name); }

    /// Generates a parser from EBNF \a grammar
    void generate(const ref<byte>& grammar);
    /// Generates a syntax tree by parsing \a input
    Node parse(const ref<byte>& input);
};
