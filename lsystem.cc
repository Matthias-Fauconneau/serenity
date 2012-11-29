#include "lsystem.h"
#include "data.h"

array<string> pool;

float Parameter::evaluate(LSystem& l, ref<float> a) const {
    if(index>=a.size) { l.parseError("Missing argument"_); return 0; }
    return a[index];
}

float Operator::evaluate(LSystem& system, ref<float> a) const {
    if(!right) { system.parseError("Missing right operand"); return 0; }
    float r=right->evaluate(system, a);
    if(op=='-' && !left) return -r;
    if(!left) { system.parseError("Missing left operand"); return 0; }
    float l=left->evaluate(system, a);
    /**/  if(op=='=') return l==r;
    else if(op=='+') return l+r;
    else if(op=='-') return l-r;
    else if(op=='>') return l>r;
    else if(op=='<') return l<r;
    else if(op=='*') return l*r;
    else if(op=='/') return l/r;
    else if(op=='^') return pow(l,r);
    else { system.parseError("Unimplemented",op); return 0; }
}

unique<Expression> LSystem::parse(const array<ref<byte> >& parameters, unique<Expression>&& e, TextData& s) {
    s.skip();
    char c = s.peek();
    if(c>='0'&&c<='9') {
        return unique<Immediate>(s.decimal());
    } else if(c=='='||c=='+'||c=='-'||c=='<'||c=='>'||c=='*'||c=='/'||c=='^') {
        s.next();
        return unique<Operator>(c, move(e), parse(parameters,move(e),s));
    } else {
        ref<byte> identifier = s.whileNo(" →=+-><*/^(),:"_);
        int i = parameters.indexOf(identifier);
        if(i>=0) return unique<Parameter>(i);
        else if(constants.contains(string(identifier))) return unique<Immediate>(constants.at(string(identifier)));
        else { parseError(name,"Unknown identifier",identifier,constants); return move(e); }
    }
}

LSystem::LSystem(string&& name, const ref<byte>& source):name(move(name)){
    for(ref<byte> line: split(source,'\n')) { currentLine++;
        TextData s(line);
        s.skip();
        if(s.match('#')) continue;
        if(line.contains(':')) s.until(':'); s.skip();
        if(!s) continue;
        if(find(line,"←"_)) {
            ref<byte> name = trim(s.until("←"_));
            s.skip();
            unique<Expression> e;
            while(s) {
                e = parse(array<ref<byte> >(),move(e),s);
                if(parseErrors) return;
                s.skip();
            }
            if(!e) { parseError("Expected expression"); return; }
            constants.insert(string(name)) = e->evaluate(*this, ref<float>());
        } else if(find(line,"→"_)) {
            ref<byte> symbol = s.identifier();
            if(!symbol) { parseError("Expected symbol",s.untilEnd()); return; }
            Rule rule(symbol);
            array<ref<byte>> parameters;
            if(s.match('(')) {
                while(!s.match(')')) {
                    s.skip();
                    ref<byte> symbol = s.identifier();
                    if(!symbol) { parseError("Expected symbol",s.untilEnd()); return; }
                    parameters << symbol;
                    s.skip();
                    if(!s.match(',') && s.peek()!=')') { parseError("Expected , or ) got",s.untilEnd()); return; }
                }
            }
            s.skip();
            if(s.match(':')) { // condition
                s.skip();
                if(!s.match('*')) {
                    unique<Expression> e;
                    while(!s.match(',') && s.peek("→"_.size)!="→"_) {
                        e = parse(parameters,move(e),s);
                        if(parseErrors) return;
                        s.skip();
                        assert(s);
                    }
                    if(!e) { parseError("Expected condition"); return; }
                    rule.condition = move(e);
                }
            }
            s.skip();
            if(!s.match("→"_)) { parseError("Expected → not \""_+s.untilEnd()+"\""_); return; }
            s.skip();
            while(s) {
                s.skip();
                ref<byte> symbol = s.identifier("$-+&^%.![]\\/|{}"_);
                if(!symbol) { parseError("Expected symbol",s.untilEnd()); return; }
                Production p(symbol);
                s.skip();
                if(s.match('(')) while(!s.match(')')) {
                    unique<Expression> e;
                    s.skip();
                    while(!s.match(',') && s.peek()!=')') {
                        e = parse(parameters,move(e),s);
                        if(parseErrors) return;
                        assert(s);
                    }
                    if(!e) { parseError("Expected argument"); return; }
                    p.arguments << move(e);
                }
                rule.productions << move(p);
            }
            rules << move(rule);
        } else {
            assert(!axiom);
            array<ref<byte>> parameters;
            for(;s;) {
                s.skip();
                ref<byte> symbol = s.identifier("$-+&^%.![]\\/|{}"_);
                if(!symbol) { parseError("Expected symbol",s.untilEnd()); return; }
                Module module (symbol);
                if(s.match('(')) while(!s.match(')')) {
                    unique<Expression> e;
                    while(!s.match(',') && s.peek()!=')') {
                        e = parse(parameters,move(e),s);
                        if(parseErrors) return;
                        s.skip();
                        assert(s);
                    }
                    if(!e) { parseError("Expected argument"); return; }
                    module.arguments << e->evaluate(*this, ref<float>());
                }
                axiom << move(module);
            }
        }
    }
}

array<Module> LSystem::generate(int level) {
    array<Module> code = copy(axiom);
    if(parseErrors) return code;
    for(int unused i: range(level)) {
        array<Module> next;
        for(uint i: range(code.size())) { const Module& c = code[i];
            array<array<Module>> matches; //bool sensitive=false;
            for(const Rule& r: rules) {
                if(r.edge!=c.symbol) continue;
                if(r.left || r.right) {
                    array<float> arguments;
                    if(r.left) {
                        array<word> path;
                        for(int j=i-1; path.size()<r.left.size() && j>=0; j--) {
                            const Module& c = code[j];
                            if(c=="]"_) { while(code[j]!="["_) j--; continue; } // skip brackets
                            if(c=="+"_ || c=="-"_ || c=="["_ /*|| ignore.contains(c)*/) continue;
                            path << c;
                            arguments << c.arguments;
                        }
                        if(path!=r.left) continue;
                    }
                    arguments << c.arguments;
                    if(r.right) {
                        array<word> path;
                        for(uint j=i+1; path.size()<r.right.size() && j<code.size(); j++) {
                            const Module& c = code[j];
                            if(c=="["_) { while(code[j]!="]"_) j++; continue; } // skip brackets
                            if(c=="+"_ || c=="-"_ /*|| ignore.contains(c)*/) continue;
                            path << c;
                            arguments << c.arguments;
                        }
                        if(path!=r.right) continue;
                    }
                    if(!r.condition->evaluate(*this, arguments)) continue;
                    //if(!sensitive) matches.clear(), sensitive=true;
                    array<Module> modules;
                    for(const Production& production: r.productions) modules << production(*this, c.arguments);
                    matches << modules;
                } else {
                    //if(sensitive) continue;
                    if(!r.condition->evaluate(*this, c.arguments)) continue;
                    array<Module> modules;
                    for(const Production& production: r.productions) modules << production(*this, c.arguments);
                    matches << modules;
                    //}
                }
            }
            assert(matches.size()<=1);
            //if(matches) next << copy(matches[random()%matches.size()]);
            if(matches) next << copy(matches[0]);
            else next << c;
        }
        if(code==next) { parseError("L-System stalled at level"_,i,code); return code; }
        code = move(next);
    }
    return code;
}
