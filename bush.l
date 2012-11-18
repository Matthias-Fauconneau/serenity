initial derivation count : N ← 6
tropism factor : tropism ← 0.02
default angle : angle ← 22.5
elongation rate : lr ← 1.109
width increase rate : wr ← 2^0.5
initial width : w0 ← 1

p1 : A(g) → !(0.4, g, 0.8)L[&H(50,w0*wr,w0)LA(g+0.1)]/////[&H(50,w0*wr,w0)LA(g+0.1)]///////[&H(50,w0*wr,w0)LA(g+0.1)]
delay : H(l,wA,wB) → F(l,wA,wB)
p2 : F(l,wA,wB) → /////F(l*lr,wA*wr,wB*wr)L
#p4 : L → ['(0, 1, 0)^^{-f+f+f-|-f+f+f}]
p4 : L → ['(0, 1, 0)^^[{.-f(10).I(1)+f(10).I(1)+f(10).I(1)}][{.+f(10).I(1)-f(10).I(1)-f(10).I(1)}]]
p4 : I(t) : t>0 → f(10).I(t-1)
p5 : I(t) : t=0 → f(10).
ω : !(0.4, 0.6, 0.8)A(0.6)
