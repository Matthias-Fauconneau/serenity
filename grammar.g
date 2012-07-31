Grammar: Input Rule*
#Grammar: Input Rule*
#Input: [^\n] '\n'
#Rule: Id ':' E*  '\n'
#E: Id | '\'' Char '\'' | '(' E ')' |  E '|' E | E '*' | E '+' | E '?' | '[' '^'? ( Char | Char '-' Char )+ ']'
Id: [A-Z] [a-z]*
#Char: [^\\] | '\\' ('n' | '\\')
