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
	enum { Null, Value, Variable, OpAssign, Prefix, Cast, Binary, Ternary } type = Null;
	explicit operator bool() { return type != Null; }
};
String str(const Expression& o) { return str(ref<string>{"Null", "Value", "Variable", "OpAssign", "Prefix", "Binary", "Ternary"}[o.type]); }

struct Parser : TextData {
	// -- Constants

	// Keywords
	const ref<string> keywords = {
		"alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch", "char", "char16_t", "char32_t", "class", "const", "constexpr", "const_cast",
		"continue", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float", "for",
		"friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "nullptr", "operator", "override", "private", "protected",
		"public", "register", "reinterpret_cast", "return", "short", "signed", "sizeof", "static", "static_cast", "struct", "switch", "template", "this",
		"thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "Type", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t",
		"while"};

	// Colors
	const bgr3f preprocessor = bgr3f(1,0,0)/2.f;
	const bgr3f keyword = bgr3f(0,1,1)/2.f;
	const bgr3f module = bgr3f(0,1,0)/2.f;
	const bgr3f typeDeclaration = bgr3f(1,0,1)/2.f;
	const bgr3f typeUse = bgr3f(1,0,1)/2.f;
	const bgr3f variableDeclaration = bgr3f(0,0,1)/2.f;
	const bgr3f variableUse = 0;
	const bgr3f memberUse = bgr3f(0,0,1)/2.f;
	const bgr3f stringLiteral = bgr3f(0,1,0)/2.f;
	const bgr3f numberLiteral = bgr3f(1,0,0)/2.f;
	const bgr3f comment = bgr3f(0,1,0)/2.f;

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

	bool matchID(string key, bgr3f color) {
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
		if(!matchID("namespace", keyword)) return false;
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
			while(!match(">")) {
				if(type()) {}
				else return backtrack();
				match(",");
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
		matchID("Type", keyword);
		matchID("const", keyword);
		if(matchID("signed", keyword) || matchID("unsigned", keyword)) {
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
		do { matchID("const", keyword); } while(match("*"));
		return commit();
	}

	// Expression

	bool initializer_list() {
		if(!match("{")) return false;
		while(!match("}")) {
			if(expression()) {}
			else error("initializer list");
			match(",");
		}
		return true;
	}

	bool number() {
		size_t begin = index;
		if(!whileDecimal()) return false;
		TextData::match("f");
		target.append(color(sliceRange(begin, index), numberLiteral));
		return true;
	}

	bool string_literal() {
		size_t begin = index;
		if(!TextData::match("\"")) return false;
		while(!TextData::match('"')) {
			if(TextData::match('\\')) advance(1);
			else advance(1);
		}
		target.append(color(sliceRange(begin, index), stringLiteral));
		return true;
	}

	bool opDot() {
		if(!((!wouldMatch("...") && match(".")) || match("->"))) return false;
		match("~"); // destructor
		if(!identifier()) error("opDot");
		return true;
	}

	bool call() {
		if(!match("(")) return false;
		while(!match(")")) {
			if(expression()) {}
			else error("opCall arguments"_);
			match(",");
		}
		return true;
	}

	bool opIndex() {
		if(!match("[")) return false;
		if(!expression()) error("opIndex argument"_);
		if(!match("]")) error("opIndex ]"_);
		return true;
	}

	bool opCast() {
		push();
		if(!type()) return backtrack();
		match("&&") || match("&");
		if(!match(")")) return backtrack();
		if(!expression()) return backtrack();
		return commit();
	}

	Expression variable() {
		size_t rewind = target.size;
		string name = identifier(variableUse);
		if(!name) return {};
		const Scope* scope = findVariable(name);
		if(!scope) return {Expression::Variable};
		target.size = rewind;
		const Location& location = scope->variables.at(name);
		assert_(location);
		target.append(color(link(name, location), scope->struct_ ? memberUse : variableUse));
		return {Expression::Variable};
	}

	Expression mutableExpression() {
		push();
		//if(type() && initializer_list()) { commit(); return}
		backtrack();
		return variable();
	}

	Expression expression() {
		if(matchAny({"!","&","*","-","+","~"})) { // prefix expression
			if(!expression()) error("prefix expression");
			return {Expression::Prefix};
		} else {
			Expression e;
			push();
			if((type() && match("(") && expression() && match(")"))) { e.type = Expression::Cast; commit(); }
			else {
				backtrack();
				if((e = mutableExpression())) { // mutableExpression op= expression
					if((!wouldMatch("==") && match("=")) || matchAny({"+=","-=","*=","/=","%=","<<=",">>="})) {
						if(!expression()) error("expression");
						return {Expression::OpAssign};
					}
				} else { // value
					e.type = Expression::Value;
					if(matchAnyID({"false","true","nullptr"}, numberLiteral)) {}
					else if(number()) {}
					else if(match("\'")) { // character literal
						size_t start = index;
						while(!TextData::match('\'')) {
							if(TextData::match('\\')) advance(1);
							else advance(1);
						}
						target.append(color(sliceRange(start, index), stringLiteral));
					}
					else if(matchID("sizeof", keyword)) {
						if(!match("(")) error("sizeof (");
						if(!type()) error("sizeof ( type");
						if(!match(")")) error("sizeof )");
					}
					else if(string_literal()) {}
					else if(match("[")) {
						while((matchID("this", keyword) || match("=") || match("&")) && match(",")) {}
						if(!match("]")) error("lambda ]");
						assert_(!stack && scopes, "lambda");
						push();
						if(!parameters()) scopes.append();
						if(!block()) error("lambda {");
						assert_(!stack, "lambda");
						scopes.pop(); assert_(scopes);
					}
					else if(matchID("this", keyword)) {}
					else if(match("(")) {
						if(opCast()) {}
						else {
							if(!(e = expression())) error("( expression", e);
							if(!match(")")) error("( expression )", e);
						}
					}
					else if(matchID("new", keyword)) {
						if(match("(")) {
							if(!expression()) error("new ( expression");
							if(!match(")")) error("new ( expression )");
						}
						if(!type()) error("new type");
						call() || initializer_list();
					}
					else if(match("delete", keyword)) {
						if(!variable()) error("delete variable");
					}
					else if(match("dynamic_cast", keyword)) {
						if(match("<") && type() && match(">") && match("(") && expression() && match(")")) {}
						else error("dynamic_cast < type > ( expression )");
					}
					else if(initializer_list()) {}
					else return {};
				}
			}

			for(;;) {
				if(opDot() || call() || opIndex() || matchID("_",0) || matchAny({"++","--","..."})) continue; // expression postfix // FIXME: e =
				 // FIXME: precedence
				string op;
				if((op = matchAny(ref<string>{"==","=","!=","<=","<",">=",">","&&","||","+","-","*","/","%"}))) { // expression binary expression
					if(!expression()) error(op, "expression");
					return {Expression::Binary};
				}
				// expression ternary expression ternary expression
				if(match("?")) {
					expression();
					if(!match(":")) error("ternary :");
					if(!expression()) error("ternary expression");
					return {Expression::Ternary};
				}
				break;
			}

			return e;
		}
	}

	// Declaration

	bool variable_name() {
		assert_(stack.size==1);
		uint location = target.size;
		string variableName = name(variableDeclaration);
		if(!variableName) return false;
		commit();
		assert_(!stack, "variable_declaration");
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
			matchID("extern", keyword) ||
			matchID("static", keyword), matchID("constexpr", keyword);
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
			}
		} else {
			if(!variable_name()) return backtrack();
			call(); // constructor
			//else commited
			while(match(",")) {
				push(); if(!variable_name()) error("variable_declaration");
				call(); // constructor
			}
		}
		assert_(!stack);
		return true;
	}

	bool templateDeclaration() {
		assert_(scopes);
		if(matchID("generic", keyword)) { scopes.append(); return true; }
		if(!matchID("template", keyword)) return false;
		if(!match("<")) error("template <");
		scopes.append();
		if(match(">")) return true; // Template specialization
		do {
			if(match("Type..."_, keyword) || matchID("Type", keyword)) {
				string typeName = name(typeDeclaration);
				if(typeName) {
					if(match("=")) {
						if(!type()) error("default type");
					}
				}
			}
			else if(type()) { push(); if(!variable_name()) backtrack(); }
			else error("templateParameters"_);
		} while(match(","));
		if(!match(">")) error("template < >"_);
		return true;
	}

	bool templateSpecialization() {
		if(match("<")) {
			while(!match(">")) {
				if(type()) { match("&&") || match("&"); }
				//else if(expression()) {}
				else error("templateSpecialization"_);
				match(",");
			}
		}
		return true;
	}

	bool using_() {
		if(!matchID("using", keyword)) return false;
		if(!identifier()) error("using identifier");
		if(!match(";")) error("using ;");
		return true;
	}

	bool parameters(bool scope=true) {
		if(!match("(")) return false;
		commit();
		assert_(!stack && scopes, "parameters");
		if(scope) scopes.append();
		while(!match(")")) {
			matchID("const", keyword);
			if(variable_declaration(true)) {}
			else error("function parameters"_);
			match(",");
		}
		return true;
	}

	bool constructor(bool isGeneric=false) {
		push();
		matchID("inline", keyword);
		matchID("explicit", keyword);
		matchID("constexpr", keyword);
		if(!(identifier() && parameters(!isGeneric))) return backtrack();
		if(!match(";")) {
			if(match(":")) {
				do {
					if(!variable()) error("initializer field");
					if(!match("(")) error("initializer (");
					if(!expression()) error("initializer expression");
					while(match(",")) if(!expression()) error("initializer expression");
					if(!match(")")) error("initializer )");
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
			if(matchID("extern", keyword)) match("\"C\"", keyword);
			else if(matchID("static", keyword)
					|| matchID("inline", keyword)
					|| matchID("virtual", keyword)) continue;
			else if(matchID("explicit", keyword)) continue;
			else if(matchID("constexpr", keyword)) continue;
			else break;
		}
		if(matchID("operator", keyword) && type()) {
			match("&&") || match("&");
		}
		else if(type() || (isGeneric && matchID("auto", typeUse))) {
			match("&&") || match("&");
			if(matchID("__attribute", keyword)) {
				if(!match("((")) error("attribute ((");
				if(!name()) error("attribute name");
				if(!match("))")) error("attribute ))");
			}
			if(matchID("operator", keyword)) {
				string op;
				if((op = matchAny({"==","=","&","->","!=","!","<=","<",">=",">","++","+","--","-","*","/","[]","()","new"}))) {
				}
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
		matchID("const", keyword);
		matchID("override", keyword);
		matchID("noexcept", keyword);
		if(match("->")) if(!type()) error("function -> type");
		if(!(block() || match(";"))) error("function");
		assert_(!stack);
		scopes.pop(); assert_(scopes);
		return true;
	}

	bool struct_(bool isGeneric) {
		push();
		if(!matchID("struct", keyword)) return backtrack();
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
		if(!matchID("typedef", keyword)) return false;
		if(!type()) error("typedef type");
		if(!name()) error("typedef name");
		if(!match(";")) error("typedef ;");
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
			else error("template");
		}
		if(using_() || typedef_() || function(false)) return true;
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
		if(!matchID("if", keyword)) return false;
		if(!match("(")) error("if (");
		Expression e;
		if(!(e = expression())) error("if ( expression");
		if(!match(")")) error("if ( expression )", e);
		body();
		if(matchID("else", keyword)) body();
		return true;
	}

	int loop = 0;
	bool while_() {
		if(!matchID("while", keyword)) return false;
		if(!match("(")) error("while (");
		expression();
		if(!match(")")) error("while )");
		loop++;
		body();
		loop--;
		return true;
	}

	bool for_() {
		if(!matchID("for", keyword)) return false;
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
		if(!matchID("do", keyword)) return false;
		loop++;
		body();
		loop--;
		if(!matchID("while", keyword)) error("while");
		if(!match("(")) error("while (");
		expression();
		if(!match(")")) error("while )");
		if(!match(";")) error("while ;");
		return true;
	}

	bool continue_() {
		if(!loop) return false;
		if(!matchID("continue", keyword)) return false;
		if(!match(";")) error("continue ;");
		return true;
	}

	bool break_() {
		if(!loop) return false;
		if(!matchID("break", keyword)) return false;
		if(!match(";")) error("break ;");
		return true;
	}

	bool return_() {
		if(!matchID("return", keyword)) return false;
		if(!expression()) error("return expression");
		if(!match(";")) error("return ;");
		return true;
	}

	bool imperativeStatement() {
		if(block() || if_() || while_() || for_() || doWhile() || return_() || continue_() || break_()) return true;
		if(expression()) {
			if(!match(";")) error("expression ;");
			return true;
		}
		return false;
	}

	void statement() {
		if(variable_declaration()) {
			if(!match(";")) error("statement"_);
		} else if(imperativeStatement()) {}
		else error("statement");
	}

	bool block() {
		if(!match("{")) return false;
		assert_(!stack, "block");
		assert_(scopes);
		scopes.append();
		while(!match("}")) statement();
		scopes.pop(); assert_(scopes);
		return true;
	}

	void body() { if(!imperativeStatement()) error("body"); }
};
