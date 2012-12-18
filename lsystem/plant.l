initial derivation count : N ← 5
tropism factor : tropism ← -0.03
default angle : angle ← 18

p1: plant → internode + [ plant + flower ] - - / / [ - - leaf ] internode [ + + leaf ] - [ plant flower ] + + plant flower
p2: internode → F seg [ / / & & leaf ] [ / / ^^ leaf ] F seg
p3: seg → seg F seg
p4: leaf → [ !(0,1,0) { + f . - f . f . - f . + | + f . - f . f . - f . } ]
p5: flower → [ & & & pedicel !(1,1,1) / wedge / / / / wedge / / / / wedge / / / / wedge / / / / wedge ]
p6: pedicel → F F
p7: wedge → [ !(0,0,1) ^ F ] [ { & & & & - f . + f . | - f . + f . } ]

ω : plant
