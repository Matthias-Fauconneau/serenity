initial derivation count : N ← 6
contraction ratio 1 : r1 ← 0.9
contraction ratio 2 : r2 ← 0.7
branching angle 1 : a1 ← 10
branching angle 2 : a2 ← 60
elongation rate : lr ← 1.109
width increase rate : wr ← 2^0.5
tropism factor : tropism ← 0.22

trunk : A(l) → F(l,wr,1) [ &(a1) B(l*r1) ] /(180) [ &(a2) B(l*r2) ]
branch : B(l) → F(l,wr,1) [ +(a1) $ B(l*r1) ] [ -(a2) $ B(l*r2) ]
growth : F(l,wA,wB) → F(l*lr,wA*wr,wB*wr)

ω : A(100)
