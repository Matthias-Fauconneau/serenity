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
	const bgr3f stringLiteral = green;

	array<uint> target;

	template<Type... Args> void error(const Args&... args) { ::error(args..., peek(16)); }

	bool space() { return target.append(toUCS4(whileAny(" \t\n"))); }
	bool match(string key, bgr3f color=black) { return target.append(::color(TextData::match(key), color)); }
	bool identifier(bgr3f color=black) {
		string identifier = TextData::identifier(":"_); // FIXME
		if(!identifier) return false;
		if(keywords.contains(identifier)) return false;
		if(!target.append(::color(identifier, color))) { error("identifier"); return false; }
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
		if(match("void")) return true;
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
			return true;
		}
		return identifier();
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
		return expression();
	}

	bool warn function() {
		templateDeclaration();
		space();
		if(!type()) error("function"_);
		space();
		if(!identifier()) error("function"_);
		space();
		skip('(');
		while(!match(")")) {
			space();
			if(match("const", keyword)) space();
			if(variable()) {}
			else error("function parameters"_);
			space();
			match(",");
		}
		space();
		skip('{');
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
		skip('{');
		space();
		while(!match("}")) {
			if(declaration()) {}
			else error("struct");
			space();
		}
		space();
		return true;
	}
	bool declaration() {
		if(variable() || function() || struct_()) {}
		else error("declaration");
		skip(';');
		return true;
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
