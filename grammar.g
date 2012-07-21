S: Input Rule*
Input: [^\n] \n
Rule: Id : E*  \n
E: Id | Token | '(' E ')' |  E '|' E | E '*' | E '+' | E '?'
Id: [A-Z] [a-z]*
Char: [^-\]] | \\ n | \\ ]
Token: [^ \n] | '[' ^? ( Char | Char - Char )+ ']'
