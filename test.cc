#include "data.h"
#include "edit.h"
#include "window.h"

struct Parser : TextData {
	const ref<string> keywords = {
		"alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch", "char", "char16_t", "char32_t", "class", "const", "constexpr", "const_cast",
		"continue", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float", "for",
		"friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "nullptr", "operator", "private", "protected", "public",
		"register", "reinterpret_cast", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "template", "this",
		"thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while"};
	const bgr3f preprocessor = blue;
	const bgr3f keyword = yellow;
	const bgr3f module = green;
	const bgr3f typeDeclaration = magenta;
	const bgr3f typeUse = magenta;
	const bgr3f variableDeclaration = magenta;
	const bgr3f variableUse = black;
	const bgr3f stringLiteral = green;
	const bgr3f numberLiteral = blue;
	const bgr3f comment = green;

	array<uint> target;

	template<Type... Args> void error(const Args&... args) { ::error(args..., "'"+slice(index-16,16)+"|"+peek(16)+"'"); }

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
	bool identifier(bgr3f color=black) {
		space();
		size_t begin = index;
		string identifier = TextData::identifier("_:"_); // FIXME
		if(!identifier) return false;
		if(keywords.contains(identifier)) { index=begin; return false; }
		target.append(::color(identifier, color));
		return true;
	}

	bool preprocessorExpression() { // TODO
		if(!target.append(toUCS4(until('\n')))) { error("preprocessorExpression"); return false; }
		return true;
	}

	bool templateArguments() {
		if(match("<")) {
			while(!match(">")) {
				if(identifier(typeUse)) {}
				else error("templateArguments"_);
				match(",");
			}
		}
		return true;
	}

	bool warn type() {
		if(matchAnyID(ref<string>{"void"_,"bool"_})) return true;
		if(!identifier(typeUse)) return false;
		templateArguments();
		return true;
	}

	bool reference() {
		return identifier();
	}

	bool dot() {
		if(wouldMatch("...") || !match(".")) return false;
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

	bool expression() {
		if(reference()) {
			// reference = expression
			if(match("=")) {
				if(!expression()) error("! expression");
			}
			// reference postfix
			while(dot() || call()) {}
			return true;
		}

		// prefix !
		if(match("!")) {
			if(!expression()) error("! expression");
			return true;
		}

		// value
		if(matchID("true", numberLiteral) || matchID("false", numberLiteral)) {}
		else if(target.append(color(whileDecimal(), numberLiteral))) {}
		else if(match("\'")) { // character literal
			size_t start = index;
			while(!TextData::match('\'')) {
				if(TextData::match('\\')) advance(1);
				else advance(1);
			}
			target.append(color(sliceRange(start, index), stringLiteral));
		}
		else if(match("\"")) { // string literal
			size_t start = index;
			while(!TextData::match('"')) {
				if(TextData::match('\\')) advance(1);
				else advance(1);
			}
			target.append(color(sliceRange(start, index), stringLiteral));
		}
		else return false;

		// value postfix
		while(dot() || call()) {}

		return true;
	}
	bool warn variable() {
		size_t begin = index;
		matchID("const", keyword);
		if(!type()) { index=begin; return false; }
		match("&");
		match("...");
		if(!identifier(variableDeclaration)) { index=begin; return false; }
		if(match("=")) {
			if(match("{")) {
				while(!match("}")) {
					if(expression()) {}
					else error("initializer list");
					match(",");
				}
			} else if(expression()) {}
			else error("initializer");
		}
		return true;
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
		while(!match("}")) statement();
		return true;
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
		if(!(block() || expression() || if_() || for_() || return_() || continue_() || break_())) error("if body");
		if(matchID("else")) if(!(block() || expression() || if_() || for_() || return_())) error("else body");
		return true;
	}

	int loop = 0;
	bool while_() {
		if(!matchID("while", keyword)) return false;
		if(!match("(")) error("while (");
		expression();
		if(!match(")")) error("while )");
		loop++;
		if(!(block() || expression() || if_() || for_())) error("while body");
		loop--;
		return true;
	}

	bool for_() {
		if(!matchID("for")) return false;
		if(!match("(")) error("for (");
		bool range = false;
		if(variable()) {
			if(match(":")) range = true;
		} else expression();
		if(!range) {
			if(!match(";")) error("for ;");
			expression();
			if(!match(";")) error("for ; ;");
			expression();
		}
		if(!match(")")) error("for )");
		loop++;
		if(!(block() || expression() || if_() || for_())) error("for body");
		loop--;
		return true;
	}

	bool statement() {
		if(if_() || while_() || for_() || return_() || continue_() || break_()) {}
		else if(declaration()) {}
		else if(expression()) {
			if(!match(";")) error("expression ;");
		}
		else if(block()) {}
		else error("statement");
		space();
		return true;
	}

	bool warn function() {
		size_t begin = index;
		templateDeclaration();
		space();
		if(matchID("const", keyword)) space();
		if(!type()) { index = begin; return false; }
		space();
		if(!identifier()) { index = begin; return false; }
		space();
		if(!match("(")) { index = begin; return false; }
		while(!match(")")) {
			space();
			if(matchID("const", keyword)) space();
			if(variable()) {}
			else error("function parameters"_);
			space();
			match(",");
		}
		space();
		if(!match("{")) error("function");
		space();
		while(!match("}")) {
			if(statement()) {}
			else error("function body"_);
			space();
		}
		return true;
	}

	bool warn struct_() {
		if(!matchID("struct", keyword)) return false;
		space();
		identifier(typeDeclaration);
		space();
		if(match(":")) {
			space();
			identifier(typeUse);
			space();
		}
		if(!match("{")) error("struct");
		space();
		while(!match("}")) {
			if(declaration()) {}
			else error("struct");
			space();
		}
		space();
		if(!match(";")) error("struct"_);
		return true;
	}
	bool warn declaration() {
		if(function()) return true;
		if(variable() || struct_()) {
			if(!match(";")) error("declaration"_);
			return true;
		}
		return false;
	}

	bool warn include() {
		if(!match("#include", preprocessor)) return false;
		space();
		if(TextData::match('"')) {
			string name = until('"');
			target.append(color('"'+link(name, name)+'"', module));
		} else { // library header
			string name = until('>');
			target.append(color(link(name, name), module));
		}
		return true;
	}
	bool warn define() {
		if(!match("#define", preprocessor)) return false;
		space();
		identifier();
		preprocessorExpression();
		return true;
	}
	bool warn condition() {
		if(!match("#if ", preprocessor)) return false;
		preprocessorExpression();
		return true;
	}


	Parser(string source) : TextData(source) {
		space();
		while(available(1)) {
			if(include() || define() || condition() || struct_()) {}
			else error("global", line() ?: peek(16));
			space();
		}
	}
};

struct Test {
	TextEdit text {move(Parser(readFile("test.cc")).target)};
	unique<Window> window = ::window(&text, 512);
} test;
