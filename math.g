Expr: Term | Expr '+' Term { value: add Expr.value Term.value } | Expr '-' Term { value: sub Expr.value Term.value }
Term: Factor | Term '*' Factor { value: mul Term.value Factor.value } | Term '/' Factor { value: div Term.value Factor.value }
Factor: [0-9]+ { value: integer } | '(' Expr ')'
