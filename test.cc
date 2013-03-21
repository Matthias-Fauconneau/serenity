#include "process.h"
#include "string.h"
#include "data.h"
struct Test {
    Test() {
        array<ref<byte>> test;
        ref<byte> key = "Test"_;
        log(key);
        test << key;
        log(test);
    }
} test;

#if 0
#include "parser.h"
#include "process.h"

struct EarleyTest {
    EarleyTest() {
        Parser parser;
        /*parser.generate(
                    R"(
                    Term: Factor
                          | Term '/' Factor
                    Factor: 'x'
                              | 'x' UnitExpr
                    UnitExpr: 'u'
                                 | 'u' '/' 'u'
                    )"_);
        const ref<byte>& input =  "xu/u"_;*/

        parser.generate(
                            R"(
                            S: M
                              | S '+' M
                            M: T
                              | M '*' T
                            T: '1'|'2'|'3'|'4'
                            )"_);
        const ref<byte>& input =  "2+3*4"_;
        parser.parse(input);
    }
} test;
#endif
#if 0
#include "window.h"
#include "text.h"
TextInput input("test"_);
Window window(&input);
#endif
