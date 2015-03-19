#include "data.h"
#include "edit.h"
#include "interface.h"
#include "window.h"

struct Parser : TextData {
	const ref<string> keywords = {
		"alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch", "char", "char16_t", "char32_t", "class", "const", "constexpr", "const_cast",
		"continue", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float", "for",
		"friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "nullptr", "operator", "override", "private", "protected",
		"public", "register", "reinterpret_cast", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "template",
		"this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t",
		"while"};
	const bgr3f preprocessor = blue/2.f;
	const bgr3f keyword = yellow/2.f;
	const bgr3f module = green/2.f;
	const bgr3f typeDeclaration = magenta/2.f;
	const bgr3f typeUse = magenta/2.f;
	const bgr3f variableDeclaration = red/2.f;
	const bgr3f variableUse = black;
	const bgr3f memberUse = red/2.f;
	const bgr3f stringLiteral = green/2.f;
	const bgr3f numberLiteral = blue/2.f;
	const bgr3f comment = green/2.f;

	::buffer<uint> fileName;
	array<uint> target;

	template<Type... Args> void error(const Args&... args) { ::error(args..., "'"+slice(index-8,8)+"|"+sliceRange(index,min(data.size,index+8))+"'"); }

	struct State { size_t source, target; };
	array<State> stack;
	void push() { stack.append({index, target.size}); }
	bool backtrack() { State state = stack.pop(); index=state.source; target.size=state.target; return false; }
	bool commit() { stack.pop(); return true; }

	typedef ::buffer<uint> Location; // module name (UCS4), index
	struct Scope {
		bool struct_ = false;
		map<string, Location> types, variables;
	};
	array<Scope> scopes;
	//const Scope* topStructScope() { for(const Scope& scope: scopes.reverse()) if(scope.struct_) return &scope; return 0; }
	const Scope* findVariable(string name) {
		for(const Scope& scope: scopes.reverse()) if(scope.variables.contains(name)) return &scope;
		return 0;
	}

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

	bool match(string key, bgr3f color=black) {
		space();
		return target.append(::color(TextData::match(key), color));
	}

	bool matchAny(ref<string> any, bgr3f color=black) {
		space();
		for(string key: any) if(match(key, color)) return true;
		return false;
	}

	bool matchID(string key, bgr3f color=black) {
		space();
		if(available(key.size)>key.size) {
			char c = peek(key.size+1).last();
			if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||"_"_.contains(c)) return false;
		}
		return target.append(::color(TextData::match(key), color));
	}

	bool matchAnyID(ref<string> any, bgr3f color=black) {
		space();
		for(string key: any) if(matchID(key, color)) return true;
		return false;
	}

	string name(bgr3f color=black) { // unqualified identifier
		space();
		size_t begin = index;
		string identifier = TextData::identifier("_"_); // FIXME
		if(!identifier) return {};
		if(keywords.contains(identifier)) { index=begin; return {}; }
		target.append(::color(identifier, color));
		return data.sliceRange(begin, index);
	}

	string identifier(bgr3f color=black) { // qualified identifier (may explicits scopes)
		space();
		size_t begin = index;
		if(match("::")) {
			if(!name(color)) error(":: name");
			while(match("::")) if(!name(color)) error(":: name");
			return data.sliceRange(begin, index);
		}
		if(!name(color)) return {};
		while(match("::")) if(!name(color)) error(":: name");
		return data.sliceRange(begin, index);
	}

	bool preprocessorExpression() { // TODO
		if(!target.append(toUCS4(until('\n')))) { error("preprocessorExpression"); return false; }
		return true;
	}

	bool templateArguments() {
		if(!wouldMatch("<=") && match("<")) {
			while(!match(">")) {
				if(identifier(typeUse)) {}
				else error("templateArguments"_);
				match(",");
			}
		}
		return true;
	}

	bool struct_() {
		if(!matchID("struct", keyword)) return false;
		identifier(typeDeclaration);
		if(match(":")) identifier(typeUse);
		if(!match("{")) error("struct");
		scopes.append({true,{},{}});
		while(!match("}")) {
			if(declaration() || constructor()) {}
			else error("struct");
		}
		scopes.pop();
		return true;
	}

	bool type() {
		if(matchAnyID(ref<string>{"void"_,"bool"_,"char"_,"int"}, typeUse)) return true; // FIXME: better remove basic types from keywords
		if(struct_()) return true;
		if(!identifier(typeUse)) return false;
		templateArguments();
		while(match("*")) {}
		if(!wouldMatch("&&")) match("&");
		return true;
	}

	bool initializer_list() {
		if(!match("{")) return false;
		while(!match("}")) {
			if(expression()) {}
			else error("initializer list");
			match(",");
		}
		return true;
	}

	bool referenceValue() {
		push();
		if(type() && initializer_list()) return commit();
		backtrack();
		size_t rewind = target.size;
		string name = identifier(variableUse);
		if(!name) return false;
		const Scope* scope = findVariable(name);
		if(!scope) return true;
		target.size = rewind;
		const Location& location = scope->variables.at(name);
		target.append(color(link(name, location), scope->struct_ ? memberUse : variableUse));
		return true;
	}

	bool member() {
		if(!((!wouldMatch("...") && match(".")) || match("->"))) return false;
		if(!identifier(variableUse)) error("operator.");
		return true;
	}

	bool call() {
		if(!match("(")) return false;
		while(!match(")")) {
			if(expression()) {}
			else error("operator() arguments"_);
			match("..."); // FIXME
			match(",");
		}
		return true;
	}

	bool binary() {
		return matchAny(ref<string>{"==","=","<=","<",">=",">","&&","||","+","-","*","/"});
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

	bool expression() {
		if(referenceValue()) {
			// referenceValue = expression
			if(match("=")) {
				if(!expression()) error("! expression");
				return true;
			}
		} else { // value
			// prefix !
			if(matchAny({"!","&","-","+"})) {
				if(!expression()) error("! expression");
				return true;
			}

			// value
			if(matchAnyID({"true","false"}, numberLiteral)) {}
			else if(number()) {}
			else if(match("\'")) { // character literal
				size_t start = index;
				while(!TextData::match('\'')) {
					if(TextData::match('\\')) advance(1);
					else advance(1);
				}
				target.append(color(sliceRange(start, index), stringLiteral));
			}
			else if(string_literal()) {}
			else if(match("(")) {
				if(expression()) {
					if(!match(")")) error("expression )");
				}
				else if(type()) {
					if(!match(")")) error("type )");
					expression();
				}
				else error("( type | expression");
			}
			else if(initializer_list()) {}
			else if(matchID("this", keyword)) {}
			else if(match("[]")) {
				if(!parameters()) scopes.append();
				if(!block()) error("lambda");
				scopes.pop();
			}
			else return false;
		}

		for(;;) {
			if(member() || call() || matchID("_") || matchAny({"++","--"})) continue; // expression postfix
			// expression binary expression
			if(binary()) {
				if(!expression()) error("binary expression");
				return true;
			}
			// expression ternary expression ternary expression
			if(match("?")) {
				expression();
				if(!match(":")) error("ternary :");
				if(!expression()) error("ternary expression");
				return true;
			}
			break;
		}

		return true;
	}

	bool variable(bool parameter=false) {
		push();
		matchID("const", keyword);
		if(!type()) return backtrack();
		if(parameter) { if(match("&&")) {} }
		match("..."); // FIXME
		uint location = index;
		string variableName = name(variableDeclaration);
		if(!variableName) return backtrack();
		scopes.last().variables.insert(variableName, fileName+location);
		if(!parameter) while(match(",")) { if(!identifier(variableDeclaration)) error("variable"); }
		if(initializer_list()) {}
		else if(match("=")) {
			if(!(initializer_list() || expression())) error("initializer");
		}
		return commit();
	}

	bool templateParameters() {
		if(match("<")) {
			while(!match(">")) {
				match("Type..."_, keyword);
				if(identifier(typeDeclaration)) {}
				else error("templateParameters"_);
				match(",");
			}
		}
		return true;
	}
	bool templateDeclaration() {
		if(!matchID("template", keyword)) return false;
		templateParameters();
		return true;
	}

	bool block() {
		if(!match("{")) return false;
		scopes.append();
		while(!match("}")) statement();
		scopes.pop();
		return true;
	}

	void body() {
		if(!(block() || imperativeStatement())) error("body");
	}

	bool return_() {
		if(!matchID("return", keyword)) return false;
		if(!expression()) error("return expression");
		if(!match(";")) error("return ;");
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

	bool if_() {
		if(!matchID("if", keyword)) return false;
		if(!match("(")) error("if statement (");
		if(!expression()) error("if expression");
		if(!match(")")) error("if statement )");
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
		if(variable()) {
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

	bool imperativeStatement() {
		if(expression()) {
			if(!match(";")) error("expression ;");
			return true;
		}
		return if_() || while_() || for_() || doWhile() || return_() || continue_() || break_();
	}

	void statement() {
		if(!(block() || declaration() || imperativeStatement())) error("statement");
	}

	bool parameters() {
		if(!match("(")) return false;
		scopes.append();
		while(!match(")")) {
			matchID("const", keyword);
			if(variable(true)) {}
			else error("function parameters"_);
			match(",");
		}
		return true;
	}

	bool function() {
		push();
		templateDeclaration();
		matchID("const", keyword);
		if(!type()) return backtrack();
		if(!identifier()) return backtrack();
		if(!parameters()) return backtrack();
		matchID("override", keyword);
		if(!block()) error("function");
		scopes.pop();
		return commit();
	}

	bool constructor() {
		push();
		if(!(identifier() && parameters())) return backtrack();
		if(match(":")) {
			do {
				if(!identifier()) error("initializer identifier");
				if(!match("(")) error("initializer (");
				if(!expression()) error("initializer expression");
				if(!match(")")) error("initializer )");
			} while(match(","));
		}
		if(!block()) error("constructor");
		scopes.pop();
		return commit();
	}

	bool typedef_() {
		if(!matchID("typedef", keyword)) return false;
		if(!type()) error("typedef type");
		if(!name()) error("typedef name");
		if(!match(";")) error("typedef ;");
		return true;
	}

	bool declaration() {
		if(typedef_() || function()) return true;
		if(variable() || struct_()) { // FIXME: struct declarations are parsed twice (first try as variable)
			if(!match(";")) error("declaration"_);
			return true;
		}
		return false;
	}

	bool include() {
		if(!match("#include", preprocessor)) return false;
		space();
		if(TextData::match('"')) {
			string name = until('"');
			target.append(color('"'+/*link(name, name)*/name+'"', module));
		} else { // library header
			string name = until('>');
			target.append(color(/*link(name, name)*/name, module));
		}
		return true;
	}

	bool define() {
		if(!match("#define", preprocessor)) return false;
		identifier();
		preprocessorExpression();
		return true;
	}

	bool condition() {
		if(!match("#if ", preprocessor)) return false;
		preprocessorExpression();
		return true;
	}

	Parser(string fileName) : TextData(readFile(fileName)), fileName(toUCS4(fileName)) {
		scopes.append();
		space();
		while(available(1)) {
			if(include() || define() || condition() || declaration()) {}
			else error("global", line() ?: peek(16));
			space();
		}
	}
};

struct ScrollTextEdit : ScrollArea {
	TextEdit edit;

	ScrollTextEdit(buffer<uint>&& text) : edit(move(text)) {}

	Widget& widget() override { return edit; }

	void ensureCursorVisible() {
		vec2 size = viewSize;
		vec2 position = edit.cursorPosition(size, edit.cursor);
		if(position.y < -offset.y) offset.y = -(position.y);
		if(position.y+edit.Text::size > -offset.y+size.y) offset.y = -(position.y-size.y+edit.Text::size);
	}

	bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus) {
		bool contentChanged = ScrollArea::mouseEvent(cursor, size, event, button, focus) || edit.mouseEvent(cursor, size, event, button, focus);
		focus = this;
		return contentChanged;
	}
	bool keyPress(Key key, Modifiers modifiers) {
		if(edit.keyPress(key, modifiers)) {
			ensureCursorVisible();
			return true;
		}
		else return ScrollArea::keyPress(key, modifiers);
	}
};

struct Test {
	ScrollTextEdit text {move(Parser("test.cc").target)};
	Test() {
		text.edit.linkActivated = [](ref<uint> identifier) { log(identifier); };
	}
	unique<Window> window = ::window(&text, 1024);
} test;
