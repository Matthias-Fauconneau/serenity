#pragma once
#include "map.h"
#include "string.h"
#include "function.h"

#include "trace.h"
#include <typeinfo>

template<Type T> struct remove_const { typedef T type; };
template<Type T> struct remove_const<const T> { typedef T type; };
#define remove_const(T) typename remove_const<T>::type

/// word is an index in a string table allowing fast move/copy/compare (but slower instanciation)
// rename to symbol ?
extern array<string> pool;
struct word {
    int id;
    word(const string& s) { id=pool.indexOf(s); if(id<0) { id=pool.size; pool<<copy(s); } }
    word(const ref<byte>& s):word(string(s)){}
    explicit operator bool() const { return pool[id].size; }
};
inline bool operator ==(word a, word b) { return a.id == b.id; }
inline const string& str(const word& w) { return pool[w.id]; }

/// Dynamically typed value used as arguments to Action (TODO: runtime type checking)
struct Value {
    virtual ~Value() {}
    /// Returns the string representation of the boxed value (for grammar debugging)
    virtual string str() const = 0;
};
inline string str(const Value& value) { return value.str(); }

/// Instance of a dynamic Value
template<Type T> struct ValueT : Value {
    T value;
    ValueT(T&& value):value(move(value)){}
    operator T() const { return value; }
    string str() const override { return string(::str(value)); }
};

/// Dynamically typed function called by parser (TODO: runtime type checking)
struct Action { virtual unique<Value> invoke(ref<const Value*>) const=0; };
/// Unpacks dynamic Value arguments to call the action implementation
template<Type F> struct ActionFunction;
template<Type O, Type T, Type... Params> struct ActionFunction<T (O::*)(Params...) const> : Action {
    function<T(Params...)> f;
    ActionFunction(function<T(Params...)> f):f(f){}

    /// Unpacks ref<unique<Value>>
    template<Type A, Type... RemainingArgs, Type... Args> unique<Value> invoke(ref<const Value*> pack, Args&&... args) const {
#if __GXX_RTTI
        auto arg = dynamic_cast<const ValueT<remove_const(remove_reference(A))>*>(pack[0]);
        assert(arg, pack[0], demangle(str(typeid(arg).name())), demangle(str(typeid(*pack[0]).name())));
#else
        const ValueT<remove_reference(A)>* arg = static_cast<const ValueT<A>*>(pack[0]);
        assert(arg);
#endif
        return invoke<RemainingArgs...>(pack.slice(1), forward<Args>(args)..., arg->value);
    }
    template<Type... RemainingArgs> unique<Value> invoke(ref<const Value*> unused empty, Params... args) const {
        assert(empty.size==0);
        return unique<ValueT<T>>( f(forward<Params>(args)...) );
    }
    unique<Value> invoke(ref<const Value*> s) const override { return invoke<Params...>(s); }
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
inline bool operator ==(const Attribute::Input& a, const Attribute::Input& b) { return a.index == b.index && a.name == b.name; }
inline string str(const Attribute::Input& v) { return str(v.index,v.name); }
inline string str(const Attribute& v) { return str(v.name,"= action("_+str(v.inputs)+")"_); }

struct Rule {
    const word symbol;
    array<word> symbols;
    array<Attribute> attributes;
    Rule(word symbol):symbol(symbol){}
    Rule(word symbol, word A):symbol(symbol){symbols<<A;}
    Rule(word symbol, word A, word B):symbol(symbol){symbols<<A<<B;}
    Rule(word symbol, array<word>&& symbols):symbol(symbol),symbols(move(symbols)){}
    uint size() const { return symbols.size; }
    word operator []( int i ) const { return symbols[i]; }
};
inline string str(const Rule& r) { return str(r.symbol)+" -> "_+str(r.symbols); }
inline bool operator==(const Rule& a, const Rule& b) { return a.symbol==b.symbol && a.symbols == b.symbols; }

struct State {
    const Rule& rule;
    uint current; // current position in rule
    uint origin; // start of the production in input

    word next() const { assert(current<rule.size()); return rule[current]; }
    bool operator==(const State& o) const { return rule==o.rule && current==o.current && origin==o.origin; }
};
inline string str(const State& o) {
    return str(o.rule.symbol)+"→"_+str(o.rule.symbols.slice(0, o.current))+"·"_+str(o.rule.symbols.slice(o.current))+","_+str(o.origin);
}

struct Node {
    word name;
    array<Node*> children;
    map<word, unique<Value>> values;
    ref<byte> input;
    Node(word name):name(name){}
};
inline string str(const Node& node) {
    if(!node.values && node.children.size==1) return str(node.children[0]);
    if(!node.values && node.children.size==2 && node.children[0]->name==node.name) return str(node.children[0])+str(node.children[1]);
    return str(node.name)+(node.values?"["_+str(node.values)+"]"_:string())+(node.children?"("_+str(node.children)+")"_:string());
}

struct Parser {
    array<Rule> rules;
    array<word> nonterminal, terminal, used;

    bool isTerminal(word w) { return terminal.contains(w); }
    bool isNonTerminal(word w) { return nonterminal.contains(w); }
    /// Parses rule expressions and generate rules from EBNF as needed
    array<word> parseRuleExpression(struct TextData& s);

    map<word, unique<Action>> actions;
    /// References an action implementation as a dynamic ActionFunction
    struct ActionRef {
        Parser* parser; word name;
        template<Type F> void operator=(F f) { parser->actions.insert(word(name), unique<ActionFunction<decltype(&F::operator())>>(f)); }
    };
    ActionRef operator[](const ref<byte>& name) { return {this,name}; }

    /// Generates a parser from an EBNF grammar
    void generate(const ref<byte>& grammar);

    array<array<State>> states;

    /// Parses \a input calling semantic actions and generating a syntax tree
    Node parse(const ref<byte>& input);

    array<Node> nodes;
    void backtrack(uint i);
};
