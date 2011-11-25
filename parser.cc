#include "process.h"
#include "file.h"
#include "map.h"
#include "string.h"

struct word {
	static array<string> unique;
	int id;
	word(const string& str) { string s=trim(str); id=unique.indexOf(s); if(id<0) { id=unique.size; unique<<move(s); } }
	bool operator ==(word o) const { return id == o.id; }
};
array<string> word::unique;
void log_(const word& w) { log_(word::unique[w.id]); }

struct Rule {
	word symbol;
	array<word> tokens;
	Rule(const string& rule) : symbol(section(rule,':')) {
		for(const string& str: split(section(rule,':',1,-1))) tokens << str;
	}
	word operator []( int i ) const { return tokens[i]; }
	bool operator ==(const Rule& o) const { return symbol == o.symbol && tokens == o.tokens; }
};
void log_(const Rule& r) { log_(r.symbol,"→ ");log_(r.tokens," "); }

struct Item {
	const Rule& rule;
	int dot;
	//word operator []( int i ) { return rule.tokens[i]; }
	word expected() const { return rule[dot]; }
	bool operator ==(const Item& o) const { return rule==o.rule && dot == o.dot; }
};
void log_(const Item& item) { log_(item.rule.symbol,"→ ");log_(item.rule.tokens.slice(0,item.dot)," ");log_(" •");log_(item.rule.tokens.slice(item.dot)," "); }

struct State {
	array<Item> items;
	map<word, int> transitions;
	bool operator ==(const State& o) const { return items==o.items; }
};
void log_(const State& state) { log_(state.items,"\n");/*log_(state.transitions);*/ }

struct Parser : Application {
	array<Rule> rules; //aka productions
	array<State> states; //aka item sets
	array<word> nonterminal, terminal;

	//transitive closure of direct read relation
	void close(array<Item>& items) { close(items,items[0]); }
	void close(array<Item>& items, Item current) {
		if(current.dot < current.rule.tokens.size) {
			for(const Rule& rule: rules) if(rule.symbol == current.expected()) {
				Item item{rule, 0};
				if(!items.contains(item)) {
					items << item;
					close(items, item);
				}
			}
		}
	}
	void computeTransitions(int current) {
		map<word, State> next;
		for(const Item& item: states[current].items) if( item.dot < item.rule.tokens.size ) next[ item.expected() ].items << item;
		for(auto i: next) { word x = i.key; State& set = i.value;
			for(Item& item: set.items) item.dot++;
			if(set.items[0].dot < set.items[0].rule.tokens.size) close(set.items);
			int j = states.indexOf(set);
			if(j<0) { j=states.size; states << move(set); computeTransitions(j); }
			states[current].transitions[x] = j;
		}
	}


	void start(array<string>&&) {
		rules << Rule("S: E");
		for(const string& rule: split(mapFile("lr0.g"),'\n')) rules << Rule(rule);
		array<word> input; input<<word("1")<<word("+")<<word("1")<<word("$");
		//log_(rules,"\n");log_("\n");
		for(const Rule& rule: rules) nonterminal.appendOnce(rule.symbol);
		for(const Rule& rule: rules) for(word token: rule.tokens) if(!nonterminal.contains(token)) terminal.appendOnce(token);
		terminal << word("$");
		//log("nonterminal:",nonterminal,"terminal:",terminal);

		State start; start.items<<Item{rules[0], 0};
		states << move(start);
		close(states[0].items);
		computeTransitions(0);

		// display item sets
		/*{int i=0; for(const auto& state: states) {
			log("Item set",i);
			log(state.items);
			i++;
		}}*/

		for(auto& set: states) {
			if(set.items.contains(Item{rules[0], 1})) set.transitions[_("$")] = 0;
			for(const auto& item: set.items) {
				int m = rules.indexOf(item.rule);
				if(m==0) continue;
				if(item.dot == item.rule.tokens.size) { //fill state with reduce
					for(word t: terminal) {
						if(set.transitions.contains(t)) fail("Conflict",set.transitions[t],item.rule);
						set.transitions[t] = -m;
					}
				}
			}
		}

		// display parsing table
		/*log_('\t');log_(array<word>(terminal+nonterminal.slice(1)),"\t");log_('\n');
		{int i=0; for(const auto& state: states) {
			log_(i,'\t');
			for(const word& key: terminal) {
				if(state.transitions.contains(key)) {
					int t = state.transitions.at(key);
					if(t>0){ log_("s");log_(t); }
					if(t<0){ log_("r");log_(-t); }
					if(t==0) log_("acc");
				}
				log_('\t');
			}
			for(const word& key: nonterminal.slice(1)) { if(state.transitions.contains(key)){ log_("g");log_(state.transitions.at(key)); } log_('\t'); }
			log_('\n');
			i++;
		}}*/

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
				stack.shrink(stack.size - rules[-action].tokens.size);
				stack << &states[stack.last()->transitions[rules[-action].symbol]];
			} else break;
		}
	}
} parser;
