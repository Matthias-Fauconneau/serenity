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

	template<Type... Args> void error(const Args&... args) { ::error(args..., "'"+peek(16)+"'"); }

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
	bool match(string key, bgr3f color=black) { return target.append(::color(TextData::match(key), color)); }
	bool matchAny(ref<string> key, bgr3f color=black) { return target.append(::color(TextData::matchAny(key), color)); }
	bool identifier(bgr3f color=black) {
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
				space();
				if(identifier(typeUse)) {}
				else error("templateArguments"_);
				space();
				match(",");
			}
		}
		return true;
	}
	bool warn type() {
		if(matchAny(ref<string>{"void"_,"bool"_})) return true;
		if(!identifier(typeUse)) return false;
		space();
		templateArguments();
		return true;
	}
	bool expression() {
		if(match("\"")) { // string literal
			size_t start = index;
			while(!TextData::match('"')) {
				if(TextData::match('\\')) advance(1);
				else advance(1);
			}
			target.append(color(sliceRange(start, index), stringLiteral));
		}
		else if(identifier()) {}
		else if(target.append(color(whileDecimal(), numberLiteral))) {}
		else return false;
		while(!wouldMatch("...") && match(".")) {
			if(!identifier(variableUse)) error("operator.");
		}
		if(match("(")) {
			while(!match(")")) {
				space();
				if(expression()) {}
				else error("operator() arguments"_);
				space();
				if(match("...")) space(); // FIXME
				match(",");
			}
		}
		return true;
	}
	bool warn variable() {
		size_t begin = index;
		match("const", keyword);
		space();
		if(!type()) { index=begin; return false; }
		space();
		if(match("&")) space();
		if(match("...")) space(); // FIXME
		if(!identifier(variableDeclaration)) { index=begin; return false; }
		space();
		if(match("=")) {
			space();
			if(match("{")) {
				while(!match("}")) {
					space();
					if(expression()) {}
					else error("initializer list");
					space();
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
				space();
				if(match("Type..."_, keyword)) space();
				if(identifier(typeDeclaration)) {}
				else error("templateParameters"_);
				space();
				match(",");
			}
		}
		return true;
	}
	bool templateDeclaration() {
		if(!match("template", keyword)) return false;
		templateParameters();
		return true;
	}

	bool statement() {
		if(match("return ")) {
			if(!expression()) error("statement");
			if(!match(";")) error("Return statement end ;");
		}
		else if(declaration()) {}
		else if(expression()) {
			if(!match(";")) error("Expression statement end ;");
		}
		else error("statement");
		space();
		return true;
	}

	bool warn function() {
		size_t begin = index;
		templateDeclaration();
		space();
		if(match("const", keyword)) space();
		if(!type()) error("return type"_);
		space();
		if(!identifier()) { index = begin; return false; }
		space();
		if(!match("(")) { index = begin; return false; }
		while(!match(")")) {
			space();
			if(match("const", keyword)) space();
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
		if(!match("struct ", keyword)) return false;
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
