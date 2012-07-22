#pragma once
#include "map.h"
#include "string.h"

/// word is an index in a string table allowing fast move/copy/compare
extern array<string> unique;
struct word {
    int id;
    word(const string& s) { id=indexOf(unique, s); if(id<0) { id=unique.size(); unique<<copy(s); } }
    word(const ref<byte>& s):word(string(s)){}
    explicit operator bool() const { return unique[id].size(); }
};
bool operator ==(word a, word b) { return a.id == b.id; }
const string& str(const word& w) { return unique[w.id]; }

struct Rule {
    const word symbol;
    array<word> tokens;
    const int original=0, end=0, from=0, to=0; const word extended=""_;
    array<word> followSet;
    Rule(word symbol):symbol(symbol){}
    Rule(word symbol, array<word>&& tokens):symbol(symbol),tokens(move(tokens)){}
    Rule(word symbol, array<word>&& tokens, int original, int end, int from, int to):
        symbol(symbol),tokens(move(tokens)),original(original),end(end),from(from),to(to),extended(str(from)+str(symbol)+str(to)){}
    uint size() const { return tokens.size(); }
    word operator []( int i ) const { return tokens[i]; }
};
string str(const Rule& r) { return str(r.extended?:r.symbol)+" -> "_+str(r.tokens); }
bool operator==(const Rule& a, const Rule& b) { return a.symbol==b.symbol; }

struct Item {
    array<Rule>& rules;
    int ruleIndex;
    uint dot;
    Item(array<Rule>& rules, int ruleIndex, uint dot):rules(rules),ruleIndex(ruleIndex),dot(dot){ assert(dot<=size()); }
    const Rule& rule() const { return rules[ruleIndex]; }
    uint size() const { return rule().size(); }
    word operator []( int i ) { return rule()[i]; }
    word expected() const { assert(dot<rule().size()); return rule()[dot]; }
};
bool operator ==(const Item& a, const Item& b) { return a.ruleIndex==b.ruleIndex && a.dot == b.dot; }
string str(const Item& item) {
    assert(item.dot<=item.size(),item.dot,item.size(),item.rule());
    return str(item.rule().symbol)+" -> "_+str(slice(item.rule().tokens, 0, item.dot))+"  ."_+str(slice<word>(item.rule().tokens, item.dot));
}

struct State {
    array<Item> items;
    map<word, int> transitions;
};
bool operator ==(const State& a, const State& b) { return a.items==b.items; }
string str(const State& state) { return str(state.items); }

struct Node { word name; array<Node> children; Node(word name):name(name){} };
string str(const Node& node) { return node.children.size()==1 ? str(node.children[0]) : str(node.name)+str(node.children," "_,"()"_); }
