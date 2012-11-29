initial derivation count : N ← 30
tropism factor : tropism ← -0.03
default angle : angle ← 18
stalk length step : stalk ← 0.5

p1 : a(t,a) : t>0 → [ /(a) &(70) L ] I(stalk,10) a(t-1,a+137.5)
p2 : a(t,a) : t=0 → [ /(a) &(70) L ] I(stalk,10) A(a+137.5)
p3 : A(a) : * → [ /(a) &(18) u(4) F I(1,10) I(1,5) X(5) K K K K ] I(stalk,8) A(a+137.5)
p4 : I(dl,t) : t>0 → F(dl) I(dl,t-1)
p5 : I(dl,t) : t=0 → F(dl)
p6 : u(t) : t>0 → &(9) u(t-1)
p7 : u(t) : t=0 → &(9)
p4 : i(dl,t) : t>0 → f(dl) i(dl,t-1)
p5 : i(dl,t) : t=0 → f(dl)
p8 : L : * → !(0,1,0) [ { . - f i(1,7) . + f i(1,7) . + f i(1,7) . } ] [ !(0,1,0) { . + f i(1,7) . - f i(1,7) . - f i(1,7) . } ]
p9 : K : * → !(1,1,1) [ & { . + f(2) . I(2,2) - - f(2) . I(2,2) } ] [ & { . - f(2) . I(2,2) + + f(2) . I(2,2) } ] /(90)
p10 : X(t) : t>0 → X(t-1)
p11 : X(t) : t=0 → ^(50) !(0,0,1) [ [ - G(2) G(2) G(2) G(2) + + [ G(2) G(2) G(2) [ + + G(2) { . ] . ] . ] . + + G(2) G(2) G(2) G(2) . - - G(2) G(2) G(2) . - - G(2) . } ] %

ω : !(0.4, 0.6, 0.8) I(1,9) a(13,0)
