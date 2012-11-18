initial derivation count : N ← 6
tropism factor : tropism ← 0.1
default angle : angle ← 22.5
elongation rate : lr ← 1.109
width increase rate : wr ← 2^0.5
initial width : w0 ← 0.5

p1 : A(b,g,r) → !(b,g,r) L [ & L H(30,w0*wr,w0) A(b-0.1,g+0.1,r-0.1)] / / / / / [ & L H(30,w0*wr,w0) A(b-0.1,g+0.1,r-0.1)] / / / / / / / [ & L H(30,w0*wr,w0) A(b-0.1,g+0.1,r-0.1) ]
delay : H(l,wA,wB) → F(l,wA,wB)
p2 : F(l,wA,wB) → / / /  / / F(l*lr,wA*wr,wB*wr)L
p4 : L → [ !(0,1,0) ^ ^ [ { . - f(15) . + f(15) . + f(15) . } ] [ { . + f(15) . - f(15) . - f(15) . } ] ]
ω : A(0.0,0.6,0.6)
