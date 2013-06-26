/*TODO
 # Volume versus minimalRadius sweep
minimalSqRadius = minimalRadius^2
volume-minimalSqRadius = $volume{minimalSqRadius}
volume-minimalRadius = SquareRootVariable
porosity-minimalRadius = Div volume-minimalRadius $volume-crop
porosity-minimalRadius-scaled = ScaleVariable porosity-minimalRadius $process-resolution
# Maximum pruning radius with volume > 0 (connectivity)
maximum-minimalRadius = Optimize volume-minimalRadius criteria='maximumArgument' constraint='>0'
*/
