initial derivation count : N ← 6
trunk contraction ratio : r1 ← 0.9
branches contraction ratio : r2 ← 0.6
trunk branching angle : a0 ← 45
lateral branching angle : a2 ← 45
trunk divergence angle : d ← 137.5
elongation rate : lr ← 1.109
width increase rate : wr ← 2^0.5
tropism factor : tropism ← 0.22

main axis : A(l) → F(l,wr,1) [ &(a0) B(l*r2) ] /(d) A(l*r1)
lateral branch : B(l) → F(l,wr,1) [ -(a2) $ C(l*r2) ] C(l*r1)
lateral branch (alternate) : C(l) → F(l,wr,1) [ +(a2) $ B(l*r2) ] B(l*r1)
growth : F(l,wA,wB) → F(l*lr,wA*wr,wB*wr)

ω : A(100)
