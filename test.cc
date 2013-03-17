#include "parser.h"
#include "process.h"

struct GLRTest {
    GLRTest() {
        Parser parser;
        parser.generate(
                    R"(
                    Term: Factor
                          | Term '/' Factor
                    Factor: 'x'
                              | 'x' UnitExpr
                    UnitExpr: 'u'
                                 | 'u' '/' 'u'
                    )"_);
        const ref<byte>& input =  "xu/u"_;
                //const ref<byte>& input =  "1GiB/100Kib/s"_;
        Node result = parser.parse(input);
        log(input,"=",result.values.at("quantity"_));
    }
} test;
