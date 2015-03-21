#pragma once
#include "data.h"
#include "map.h"
#include "vector.h"
#include "text.h"

typedef buffer<uint> Location; // module name (UCS4), index
template<> Location copy(const Location& o) { return copyRef(o); }

struct Scope {
	bool struct_ = false;
	map<String, Location> types, variables;
	map<string, Scope> scopes;
};
template<> Scope copy(const Scope& o) { return Scope{o.struct_, copy(o.types), copy(o.variables), copy(o.scopes)}; }
String str(const Scope& o) { return str(o.variables, o.scopes); }

struct Expression {
	enum ExprType { Null, Value, Variable, Member, OpAssign, Prefix, Cast, Postfix, Binary, Ternary } type;
	buffer<Expression> children;
	string arg;
	Expression(ExprType type=Null, mref<Expression> children={}, string arg={}) : type(type), children(moveRef(children)), arg(arg) {}
	explicit operator bool() { return type != Null; }
};
String str(const Expression& o) {
	return str(ref<string>{"Null", "Value", "Variable", "Member", "OpAssign", "Prefix", "Cast", "Postfix", "Binary", "Ternary"}[o.type])
			+str(o.children)+o.arg;
}

// -- Constants

// Colors
static const bgr3f preprocessor = bgr3f(1,0,0)/2.f;
static const bgr3f keyword = bgr3f(0,1,1)/2.f;
static const bgr3f module = bgr3f(0,1,0)/2.f;
static const bgr3f typeDeclaration = bgr3f(1,0,1)/2.f;
static const bgr3f typeUse = bgr3f(1,0,1)/2.f;
static const bgr3f variableDeclaration = bgr3f(0,0,1)/2.f;
static const bgr3f variableUse = 0;
static const bgr3f memberUse = bgr3f(0,0,1)/2.f;
static const bgr3f stringLiteral = bgr3f(0,1,0)/2.f;
static const bgr3f numberLiteral = bgr3f(1,0,0)/2.f;
static const bgr3f comment = bgr3f(0,1,0)/2.f;

struct Parser : TextData {
	// Keywords
	const ref<string> keywords = {
		"alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch", "char", "char16_t", "char32_t", "class", "const", "constexpr", "const_cast",
		"continue", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float", "for",
		"friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "nullptr", "operator", "override", "private", "protected",
		"public", "register", "reinterpret_cast", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "template",
		"this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "Type", "union", "unsigned", "using", "virtual", "void", "volatile",
		"wchar_t", "while"};

	// Files
	const array<String> sources = currentWorkingDirectory().list(Files|Recursive);
	::function<void(array<Scope>&, string)> parse;
	::buffer<uint> fileName;

	/// Returns the first path matching file
	String find(string name) {
		for(string path: sources) if(path == name) return copyRef(path); // Full path match
		for(string path: sources) if(section(path,'/',-2,-1) == name) return copyRef(path); // File name match
		error("No such file", name, "in", sources);
		return {};
	}

	// Context
	array<Scope>& scopes; // Symbol table
	struct State { size_t source, target; };
	array<State> stack; // Backtrack stack

	// Output
	array<uint> target; // Source text with colors and navigation links

	// -- Helpers

	template<Type... Args> void error(const Args&... args) {
		::error(toUTF8(fileName)+':'+str(lineIndex)+':'+str(columnIndex)+':',
				args..., "'"+sliceRange(max(0,int(index)-8),index)+"|"+sliceRange(index,min(data.size,index+32))+"'");
	}

	// Backtrack
	void push() { stack.append({index, target.size}); }
	bool backtrack() { State state = stack.pop(); index=state.source; target.size=state.target; return false; }
	bool commit() { assert_(stack); stack.pop(); return true; }

	// Symbol table
	const Scope* findScope(string name) {
		for(const Scope& scope: scopes.reverse()) if(scope.scopes.contains(name)) return &scope.scopes.at(name);
		return 0;
	}
	const Scope* findType(string name) {
		for(const Scope& scope: scopes.reverse()) if(scope.types.contains(name)) return &scope;
		return 0;
	}
	const Scope* findVariable(string name) {
		for(const Scope& scope: scopes.reverse()) {
			if(scope.variables.contains(name)) return &scope;
		}
		return 0;
	}

	// -- -- Parser

	// -- Whitespace / tokens

	bool space() {
		for(;;) {
			if(target.append(toUCS4(whileAny(" \t\n")))) continue;
			size_t begin = index;
			if(TextData::match("//")) {
				until('\n');
				target.append(::color(sliceRange(begin, index), comment));
				continue;
			}
			if(TextData::match("/*")) {
				until("*/");
				target.append(::color(sliceRange(begin, index), comment));
				continue;
			}
			break;
		}
		return true;
	}

	bool wouldMatch(string key) {
		space();
		return TextData::wouldMatch(key);
	}

	bool match(string key, bgr3f color=0) {
		space();
		return target.append(::color(TextData::match(key), color));
	}

	string matchAny(ref<string> any, bgr3f color=0) {
		space();
		for(string key: any) if(match(key, color)) return key;
		return {};
	}

	bool matchID(string key, bgr3f color=keyword) {
		space();
		if(available(key.size)>key.size) {
			char c = peek(key.size+1).last();
			if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||"_"_.contains(c)) return false;
		}
		return target.append(::color(TextData::match(key), color));
	}

	bool matchAnyID(ref<string> any, bgr3f color) {
		space();
		for(string key: any) if(matchID(key, color)) return true;
		return false;
	}

	// -- Preprocessor

	bool pragma() {
		if(!matchID("#pragma", preprocessor)) return false;
		if(!matchID("once", preprocessor)) return false;
		return true;
	}

	bool include() {
		if(!matchID("#include", preprocessor)) return false;
		space();
		if(TextData::match('"')) {
			string name = until('"');
			parse(scopes, find(name));
			target.append(color(uint('"')+link(name, toUCS4(name)+1u/*FIXME: 0u*/)+uint('"'), module));
		} else { // library header
			string name = until('>');
			target.append(color(/*link(name, name)*/name, module));
		}
		return true;
	}

	bool ppExpression() {
		size_t begin = index;
		while(!TextData::match('\n')) {
			if(TextData::match("\\\n")) {}
			else advance(1);
		}
		return target.append(toUCS4(sliceRange(begin, index)));
	}

	bool define() {
		if(!matchID("#define", preprocessor)) return false;
		identifier();
		ppExpression();
		return true;
	}

	bool ppIf() {
		if(!matchID("#if", preprocessor)) return false;
		ppExpression();
		while(global()) {}
		if(matchID("#else", preprocessor)) while(global()) {}
		if(!matchID("#endif", preprocessor)) error("#if");
		return true;
	}

	// -- Global
	bool namespace_() {
		if(!matchID("namespace")) return false;
		if(!name()) error("namespace");
		if(!match("{")) error("namespace");
		while(declaration()) {}
		if(!match("}")) error("namespace");
		return true;
	}

	bool global() { return pragma() || include() || define() || ppIf() || declaration() || namespace_(); }

	Parser(string fileName, array<Scope>& scopes, ::function<void(array<Scope>&, string)> parse)
		: TextData(readFile(fileName)), parse(parse), fileName(toUCS4(fileName)), scopes(scopes) {
		space();
		while(global()) {}
		if(available(1)) error("global");
		assert_(scopes.size == 1 && stack.size == 0, scopes.size, stack.size);
	}

	// -- Type
	string name(bgr3f color=0) { // unqualified identifier
		space();
		size_t begin = index;
		string identifier = TextData::identifier("_"_); // FIXME
		if(!identifier) return {};
		if(keywords.contains(identifier)) { index=begin; return {}; }
		target.append(::color(identifier, color));
		return data.sliceRange(begin, index);
	}

	string identifier(bgr3f color=0) { // qualified identifier (may explicits scopes)
		space();
		size_t begin = index;
		push();
		match("::");
		do {
			if(!name(color)) { backtrack(); return {}; }
			templateArguments();
		} while(match("::")); // FIXME: check type table, finish with name
		commit();
		return sliceRange(begin, index);
	}

	bool templateArguments() {
		push();
		if(!wouldMatch("<=") && match("<")) {
			if(!match(">")) {
				do {
					push();
					if(type() && (wouldMatch(",") || wouldMatch(">"))) commit();
					else {
						backtrack();
						if(expression(true)) {}
						else { error("template"); return backtrack(); }
					}
				} while(match(","));
				if(!match(">")) return backtrack();
			}
		}
		return commit();
	}

	bool type() {
		push();
		if(match("decltype")) {
			if(!(match("(") && expression() && match(")"))) error("decltype(expression)");
			return commit();
		}
		matchID("Type");
		matchID("const");
		if(matchID("signed", typeUse) || matchID("unsigned", typeUse)) {
			if(!matchAnyID(ref<string>{"char"_,"short","int","long long","long"}, typeUse)) error("type");
		}
		else if(matchAnyID(ref<string>{"void"_,"bool"_,"char"_,"short","int","float","long long","long","double"}, typeUse)) {}
		else {
			match("::");
			do {
				if(!name(typeUse)) return backtrack();
				templateArguments();
			} while(match("::"));
		}
		do { matchID("const"); } while(match("*"));
		return commit();
	}

	// Expression

	Expression keywordLiteral() {
		if(matchAnyID({"false","true","nullptr"}, numberLiteral)) return {Expression::Value};
		return {};
	}

	Expression number() {
		size_t begin = index;
		if(!whileDecimal()) return {};
		TextData::matchAny("fu");
		target.append(color(sliceRange(begin, index), numberLiteral));
		return {Expression::Value};
	}

	Expression character_literal() {
		if(!match("\'")) return {};
		size_t start = index;
		while(!TextData::match('\'')) {
			if(TextData::match('\\')) advance(1);
			else advance(1);
		}
		target.append(color(sliceRange(start, index), stringLiteral));
		return {Expression::Value};
	}

	Expression string_literal() {
		space();
		size_t begin = index;
		if(!TextData::match("\"")) return {};
		while(!TextData::match('"')) {
			if(TextData::match('\\')) advance(1);
			else advance(1);
		}
		target.append(color(sliceRange(begin, index), stringLiteral));
		return {Expression::Value};
	}

	Expression sizeofPack() {
		push();
		if(match("sizeof...") && match("(") && name() && match(")")) { commit(); return {Expression::Value}; }
		backtrack(); return {};
	}

	Expression sizeof_() {
		push();
		if(match("sizeof") && match("(") && type() && match(")")) { commit(); return {Expression::Value}; }
		backtrack(); return {};
	}

	Expression initializer_list() {
		push();
		if(match("{")) {
			if(!match("}")) {
				do {
					if(!expression()) { backtrack(); return {}; }
				} while(match(","));
				if(!match("}")) { backtrack(); return {}; }
			}
			commit(); return {Expression::Value};
		}
		backtrack(); return {};
	}

	Expression lambda() {
		if(!match("[")) return {};
		while((matchID("this") || match("=") || match("&")) && match(",")) {}
		if(!match("]")) error("lambda ]");
		push();
		if(!parameters()) scopes.append();
		if(match("->")) if(!type()) error("-> type");
		if(!block()) error("lambda {");
		scopes.pop();
		return {Expression::Value};
	}

	Expression this_() {
		if(!matchID("this")) return {};
		return {Expression::Value};
	}

	Expression variable() {
		size_t rewind = target.size;
		string name = identifier(variableUse);
		if(!name) return {};
		const Scope* scope = findVariable(name);
		if(scope) {
			target.size = rewind;
			const Location& location = scope->variables.at(name);
			assert_(location);
			target.append(color(link(name, location), scope->struct_ ? memberUse : variableUse));
		}
		return {Expression::Variable,{}, name};
	}

	Expression opDot(Expression& a) {
		if(!((!wouldMatch("...") && match(".")) || match("->"))) return {};
		match("~"); // destructor
		string b = identifier();
		if(!b) error("opDot");
		return {Expression::Member, {move(a)}, b};
	}

	Expression opCall() {
		push();
		if(match("(")) {
			if(!match(")")) {
				do {
					if(!expression()) { error("opCall Expression"); backtrack(); return {}; }
				} while(match(","));
				if(!match(")")) { error("opCall )"); backtrack(); return {}; }
			}
			commit(); return {Expression::Value};
		}
		backtrack(); return {};
	}

	Expression opIndex() {
		if(!match("[")) return {};
		if(!expression()) error("opIndex argument"_);
		if(!match("]")) error("opIndex ]"_);
		return {Expression::Value};
	}

	Expression opCast() {
		push();
		if(match("(") && type()) {
			match("&&") || match("&");
			if(match(")") && expression()) { commit(); return {Expression::Value}; }
		}
		backtrack(); return {};
	}

	Expression opBracket() {
		push();
		if(match("(") && expression() && match(")")) { commit(); return {Expression::Value}; }
		else { backtrack(); return {}; }
	}

	Expression opPrefix() {
		push();
		string op = matchAny({"!","&","*","-","+","~"});
		if(op && expression()) { commit(); return {Expression::Prefix,{}, op}; }
		else { backtrack(); return {}; }
	}

	/*Expression constructorCast() { = constructorCall
		push();
		if((type() && match("(") && expression() && match(")"))) { commit(); return {Expression::Cast}; }
		backtrack(); return {};
	}*/

	Expression constructorCall() {
		push();
		if(type() && (opCall() || initializer_list())) { commit(); return {Expression::Value}; }
		backtrack(); return {};
	}

	Expression opNew() {
		push();
		if(matchID("new")) {
			push();
			if(match("(") && expression() && match(")")) commit();
			else backtrack();
			if(type()) {
				commit();
				Expression e {Expression::Value};
				return opCall() ?: initializer_list() ?: move(e);
			}
		}
		backtrack(); return {};
	}

	Expression opDelete() {
		push();
		if(matchID("delete") && variable()) { commit(); return {Expression::Value}; }
		backtrack(); return {};
	}

	Expression opDynamicCast() {
		push();
		if(matchID("dynamic_cast") && match("<") && type() && match(">") && match("(") && expression() && match(")")) {
			commit(); return {Expression::Value};
		}
		backtrack(); return {};
	}

	Expression opAssign(Expression& a) { // TODO: mutable a
		push();
		if(((!wouldMatch("==") && match("=")) || matchAny({"+=","-=","*=","/=","%=","<<=",">>="}))) {
			Expression b = expression();
			if(b) { commit(); return {Expression::OpAssign,{move(a),move(b)}}; }
			else error(b);
		}
		backtrack(); return {};
	}

	Expression opBinary(Expression& a, bool templateArgument) { // Expr op Expr
		push();
		string op = matchAny(ref<string>{"==","=","!=","<=","<",">=","&&","||","+","-","*","/","%","<<",">>","&","|"});
		if(!op && !templateArgument && match(">")) op=">"; // Workaround to stop too greedy expression parse of template arguments
		if(op) {
			Expression b = expression(templateArgument);
			if(b) {
				commit();
				return {Expression::Binary, {move(a), move(b)}, op};
			}
		}
		backtrack(); return {};
	}

	Expression opPostfix() {
		if(matchAny({"++","--","...","_"})) return {Expression::Postfix};
		return {};
	}

	Expression opTernary() {
		push();
		if(match("?") && (expression(), match(":"))) { expression(); commit(); return {Expression::Ternary}; }
		backtrack(); return {};
	}

	Expression expression(bool templateArgument=false) {
		Expression e = /*constructorCast() ?:*/ constructorCall() ?: variable() ?: opPrefix() ?: opDynamicCast() ?: opCast() ?: opBracket()
						   ?: keywordLiteral() ?: number() ?: character_literal() ?: string_literal() ?: sizeofPack() ?: sizeof_() ?: initializer_list() ?: lambda() ?: this_()
						   ?: opNew() ?: opDelete();
		if(e) for(;;) { // Closes left recursion
			Expression r = opAssign(e) ?: opBinary(e, templateArgument) ?: opDot(e) ?: opCall() ?: opIndex() ?: opPostfix() ?: opTernary() ?: Expression();
			if(!r) break;
			e = move(r);
		}
		return move(e);
	}

	// Declaration

	bool variable_name() {
		uint location = target.size;
		string variableName = name(variableDeclaration);
		if(!variableName) return false;
		commit();
		if(scopes.last().variables.contains(variableName)) error("duplicate declaration", variableName, scopes.last().variables.keys);
		scopes.last().variables.insert(copyRef(variableName), fileName+max(1u,location));
		if(initializer_list()) {}
		else if(match("=")) {
			if(!(initializer_list() || expression())) error("initializer");
		}
		return true;
	}

	bool variable_declaration(bool parameter=false) {
		push();
		if(!parameter) {
			matchID("extern") ||
			matchID("static"), matchID("constexpr");
		}
		if(!type()) return backtrack();
		if(parameter) match("&&");
		match("&");
		if(parameter) {
			if(match("(")) {
				if(!match("&")) return backtrack();
				if(!variable_name()) return backtrack();
				// else commited
				if(!match(")")) error("static array parameter )");
				if(!match("[")) error("static array parameter [");
				if(!expression()) error("static array parameter");
				if(!match("]")) error("static array parameter ]");
			}
			else {
				match("...");
				if(!variable_name()) commit();
				//else commited
				if(match("[")) {
					if(!(expression() && match("]"))) error("[ expression ]");
				}
			}
		} else {
			if(!variable_name()) return backtrack();
			opCall(); // constructor
			//else commited
			while(match(",")) {
				push(); if(!variable_name()) error("variable_declaration");
				opCall(); // constructor
			}
		}
		return true;
	}

	bool templateDeclaration() {
		assert_(scopes);
		if(matchID("generic")) { scopes.append(); return true; }
		if(!matchID("template")) return false;
		if(!match("<")) error("template <");
		scopes.append();
		if(!match(">")) {
			do {
				if(matchID("Type")) {
					match("...");
					string typeName = name(typeDeclaration);
					if(typeName) {
						if(match("=")) {
							if(!type()) error("default type");
						}
					}
				}
				else if(type()) { push(); if(!variable_name()) backtrack(); }
				else if(templateDeclaration() && matchID("Type")) { push(); if(!variable_name()) backtrack(); }
				else error("template declaration"_);
			} while(match(","));
			if(!match(">")) error("template < >"_);
		}
		return true;
	}

	bool templateSpecialization() {
		if(match("<")) {
			if(!match(">")) {
				do {
					if(type()) { match("&&") || match("&"); }
					//else if(expression()) {}
					else error("templateSpecialization"_);
				} while(match(","));
				if(!match(">")) error("templateSpecialization"_);
			}
		}
		return true;
	}

	bool using_() {
		if(!matchID("using")) return false;
		if(!identifier()) error("using identifier");
		if(!match(";")) error("using ;");
		return true;
	}

	bool parameters(bool scope=true) {
		if(!match("(")) return false;
		commit();
		if(scope) scopes.append();
		if(!match(")")) {
			do {
				matchID("const");
				if(variable_declaration(true)) {}
				else error("function parameters"_);
			} while(match(","));
			if(!match(")")) error("parameters");
		}
		return true;
	}

	bool constructor(bool isGeneric=false) {
		push();
		matchID("inline");
		matchID("explicit");
		matchID("constexpr");
		if(!(identifier() && parameters(!isGeneric))) return backtrack();
		if(!match(";")) {
			if(match(":")) {
				do {
					if(!variable()) error("initializer");
					if(match("{")) {
						if(!match("}")) {
							do {
								if(!expression()) error("initializer expression");
							} while(match(","));
							if(!match("}")) error("initializer }");
						}
					}
					else if(match("(")) {
						if(!match(")")) {
							do {
								if(!expression()) error("initializer expression");
							} while(match(","));
							if(!match(")")) error("initializer }");
						}
					}
					else error("initializer ( or {");
				} while(match(","));
			}
			if(!block()) error("constructor");
		}
		assert_(!stack);
		scopes.pop();
		assert_(scopes);
		return true;
	}

	bool destructor() {
		if(!match("~")) return false;
		if(!(name() && match("(") && match(")"))) error("destructor");
		if(!block()) error("constructor");
		assert_(!stack);
		return true;
	}

	bool function(bool isGeneric = false) {
		push();
		for(;;) {
			if(matchID("extern")) match("\"C\"", keyword);
			else if(matchID("static")
					|| matchID("inline")
					|| matchID("virtual")) continue;
			else if(matchID("explicit")) continue;
			else if(matchID("constexpr")) continue;
			else break;
		}
		if(matchID("operator") && type()) {
			match("&&") || match("&");
		}
		else if(type() || (isGeneric && matchID("auto", typeUse))) {
			match("&&") || match("&");
			if(matchID("__attribute")) {
				if(!match("((")) error("attribute ((");
				if(!name()) error("attribute name");
				if(!match("))")) error("attribute ))");
			}
			if(matchID("operator")) {
				string op = matchAny({"==","=","&","->","!=","!","<=","<",">=",">","++","+","--","-","*","/","[]","()","new"});
				if(op) {}
				else if(match("\"\"")) {
					if(!name()) error("operator \"\"name");
				}
				else error("operator");
			}
			else if(identifier()) {}
			else return backtrack();
		}
		else return backtrack();
		if(!parameters(!isGeneric)) return backtrack();
		matchID("const");
		matchID("override");
		matchID("noexcept");
		if(match("->")) if(!type()) error("function -> type");
		if(!(block() || match(";"))) error("function");
		assert_(!stack);
		scopes.pop(); assert_(scopes);
		return true;
	}

	bool struct_(bool isGeneric) {
		push();
		if(!matchID("struct")) return backtrack();
		commit();
		assert_(!stack && scopes, "struct");
		if(!isGeneric) scopes.append({true,{},{},{}});
		string name = identifier(typeDeclaration);
		templateSpecialization();
		if(match(":")) {
			do {
				size_t begin = index;
				if(!type()) error("base");
				string baseID = sliceRange(begin, index);
				const Scope* base = findScope(baseID);
				if(base) {
					scopes.last().types.append(base->types);
					scopes.last().variables.append(base->variables);
				}
			} while(match(","));
			if(!wouldMatch("{")) error("struct");
		}
		if(match("{")) {
			while(!match("}")) {
				if(declaration() || constructor() || destructor()) {}
				else error("struct: declaration | constructor | destructor");
			}
			assert_(!stack, "struct");
			Scope s = scopes.pop();
			assert_(scopes);
			scopes.last().scopes[name].variables.append(move(s.variables));
		} else { scopes.pop(); assert_(scopes); }
		return true;
	}

	bool typedef_() {
		if(!matchID("typedef")) return false;
		if(!type()) error("typedef type");
		if(!name()) error("typedef name");
		if(!match(";")) error("typedef ;");
		return true;
	}

	bool static_assert_() {
		if(!matchID("static_assert")) return false;
		if(!(match("(") && expression())) error("static_assert ( expression");
		if(match(",")) if(!string_literal()) error("static_assert ( expression, literal");
		if(!(match(")") && match(";"))) error("static_assert ( expression )");
		return true;
	}

	bool declaration() {
		if(templateDeclaration()) {
			if(constructor(true) || function(true)) return true;
			else if(struct_(true)) {
				push();
				if(!variable_name()) backtrack();
				if(!match(";")) error("declaration ;"_);
				return true;
			}
			else error("template function or struct");
		}
		if(using_() || typedef_() || static_assert_() || function(false)) return true;
		if(struct_(false)) {
			push();
			if(!variable_name()) backtrack();
			if(!match(";")) error("declaration"_);
			return true;
		}
		if(variable_declaration()) {
			if(!match(";")) error("declaration"_);
			return true;
		}
		return false;
	}

	// Statements

	bool if_() {
		if(!matchID("if")) return false;
		if(!match("(")) error("if (");
		Expression e;
		if(!(e = expression())) error("if ( expression");
		if(!match(")")) error("if ( expression )", e);
		body();
		if(matchID("else")) body();
		return true;
	}

	int loop = 0;
	bool while_() {
		if(!matchID("while")) return false;
		if(!match("(")) error("while (");
		expression();
		if(!match(")")) error("while )");
		loop++;
		body();
		loop--;
		return true;
	}

	bool for_() {
		if(!matchID("for")) return false;
		if(!match("(")) error("for (");
		bool range = false;
		if(variable_declaration()) {
			if(match(":"_)) range = true;
		} else expression();
		if(!range) {
			if(!match(";")) error("for ;");
			expression();
			if(!match(";")) error("for ; ;");
		}
		expression();
		if(!match(")")) error("for )");
		loop++;
		body();
		loop--;
		return true;
	}

	bool doWhile() {
		if(!matchID("do")) return false;
		loop++;
		body();
		loop--;
		if(!matchID("while")) error("while");
		if(!match("(")) error("while (");
		expression();
		if(!match(")")) error("while )");
		if(!match(";")) error("while ;");
		return true;
	}

	bool continue_() {
		if(!loop) return false;
		if(!matchID("continue")) return false;
		if(!match(";")) error("continue ;");
		return true;
	}

	bool break_() {
		if(!loop) return false;
		if(!matchID("break")) return false;
		if(!match(";")) error("break ;");
		return true;
	}

	bool return_() {
		if(!matchID("return")) return false;
		if(!expression()) error("return expression");
		if(!match(";")) error("return ;");
		return true;
	}

	bool imperativeStatement() {
		if(block() || if_() || while_() || for_() || doWhile() || return_() || continue_() || break_()) return true;
		Expression e = expression();
		if(e) {
			if(!match(";")) error("expression ;", e);
			return true;
		}
		return false;
	}

	void statement() {
		if(static_assert_()) {}
		else if(variable_declaration()) {
			if(!match(";")) error("statement"_);
		}
		else if(imperativeStatement()) {}
		else error("statement");
	}

	bool block() {
		if(!match("{")) return false;
		scopes.append();
		while(!match("}")) statement();
		scopes.pop();
		return true;
	}

	void body() { if(!imperativeStatement()) error("body"); }
};
