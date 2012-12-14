initial derivation count : N ← 6
divergence angle 1 : d1 ← 94.74
divergence angle 2 : d2 ← 132.63
branching angle : a ← 18.95
elongation rate : lr ← 1.109
width increase rate : wr ← 3^0.5
tropism factor : tropism ← 0.22
default angle : angle ← 22.5

ternary branch : A(g) → F1(50,wr,wr) [ &(a) F1(50,wr,1) L(g) A(g+1) ] /(d1) [ &(a) F1(50,wr,1) L(g) A(g+1) ] /(d2) [ &(a) F1(50,wr,1) L(g) A(g+1) ]
delayed twigs : F1(l,wA,wB) → F(l,wA,wB)
growth : F(l,wA,wB) → F(l*lr,wA*wr,wB*wr)

leaf size: size ← 20
leaf : L(g) : g>2→ [ !(0,0.7,0) ^ [ { . - f(size) . + f(size) . + f(size) . } ] [ { . + f(size) . - f(size) . - f(size) . } ] ]

ω : !(0.3,0.4,0.7) F1(200,1,1) /(45) A(0)

