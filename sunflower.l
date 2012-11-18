initial derivation count : N ← 256
divergence angle : a ← 137.5

edge count : eN ← 12
rotation angle : θ ← 360/eN
side length : l ← 3.14/eN
: S(n) : n>0 → F(l)+(θ)S(n-1)
D → [{S(eN)}]

p1 : A(n) : * → +(a)[f(n^0.5)D]A(n+1)
ω : f(1)^(90)A(0)
