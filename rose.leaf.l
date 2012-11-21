initial derivation count : N ← 25
default angle : angle ← 60

initial length - main segment : LA ← 5
growth rate - main segment : RA ← 1.15
initial length - lateral segment : LB ← 1.3
growth rate - lateral segment : RB ← 1.25
initial length - marginal notch : LC ← 3
growth rate - marginal notch : RC ← 1.19

p1 : A(t,d) : d=0 → . G(LA,RA,1) . [ + B(t) G(LC,RC,t) . } ] [ + B(t) { . ] A(t+1,d)
p2 : A(t,d) : d=1 → . G(LA,RA,1) . [ - B(t) G(LC,RC,t) . } ] [ - B(t) { . ] A(t+1,d)
p3 : B(t) : t>0 → G(LB,RB,1) B(t-1)
p4 : G(s,r,t) : t=1 → G(s*r,r,t)
p5 : G(s,r,t) : t>1 → G(s*r,r,t-1)
ω : [ { A(0,0) . } ] [ { A(0,1) . } ]
