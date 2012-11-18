initial derivation count : N ← 30
tropism factor : tropism ← -0.03
default angle : angle ← 18
stalk length step : stalk ← 0.5

p1 : a(t) : t>0 → [ &(70) L ] /(137.5) I(stalk,10) a(t-1)
p2 : a(t) : t=0 → [ &(70) L ] /(137.5) I(stalk,10) A
p3 : A : * → [ &(18) u(4) F I(1,10) I(1,5) X(5) K K K K ] /(137.5) I(stalk,8) A
p4 : I(dl,t) : t>0 → F(dl) I(dl,t-1)
p5 : I(dl,t) : t=0 → F(dl)
p6 : u(t) : t>0 → &(9) u(t-1)
p7 : u(t) : t=0 → &(9)
p8 : L : * → !(0,1,0) [ { . - F I(1,7) + F I(1,7) + F I(1,7) } ] [ !(0,1,0) { . + F I(1,7) - F I(1,7) - F I(1,7) } ]
p9 : K : * → !(1,1,1) [ & { . + F(2) I(2,2) - - F(2) I(2,2) } ] [ & { . - F(2) I(2,2) + + F(2) I(2,2) } ] /(90)
p10 : X(t) : t>0 → X(t-1)
p11 : X(t) : t=0 → ^(50) !(0,0,1) [ [ - G(2) G(2) G(2) G(2) + + [ G(2) G(2) G(2) [ + + G(2) { . ] . ] . ] . + + G(2) G(2) G(2) G(2) . - - G(2) G(2) G(2) . - - G(2) . } ] %

ω : !(0.4, 0.6, 0.8) I(1,9) a(13)
