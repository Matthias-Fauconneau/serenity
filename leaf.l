#cordate leaf
#initial derivation count : N ← 6
#default angle : angle ← 10
#p1 : A → [ + A { . ] . C . }
#p2 : B → [ - B { . ] . C . }
#p3 : C → G C
#ω : F ^(90) [ A ] [ B ]

initial derivation count : N ← 20
default angle : angle ← 60
initial length - main segment : LA ← 5
growth rate - main segment : RA ← 1.1
initial length - lateral segment : LB ← 1
growth rate - lateral segment : RB ← 1.2
growth potential decrement : PD ← 1

p1 : A(t) : * → G(LA,RA) [ - B(t) . ] [ A(t+1) ] [ + B(t) . ]
p2 : B(t) : t>0 → G(LB,RB) B(t-PD)
p3 : G(s,r) : * → G(s*r,r)
ω : { . A(0) }
