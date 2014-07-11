 Tomography is written using [C++11][http://en.wikipedia.org/wiki/C++11]

### Tomographic reconstruction configurations evaluation

\ref tomography/compute.cc "compute" generates a synthetic sample of porous rock and evaluates reconstruction methods over a range of configurations.
For each configuration, 4 files are saved in a "Results" under the current working directory:
- The normalized mean square error over the central and extreme parts of the volumes are stored for each iterations
- The best reconstruction volume
- Two slices of the best reconstruction

## Arguments
- ui [=false]
 Displays a window with informations to monitor the reconstruction as it runs.
- reference [=false]
 Instead of running any reconstructions, exports the reference volumes slices in a "Reference" folder and as many analytic projections to "Projection".
- update = missing|best|all
 When set to "missing", only results missing from "Results" are computed
 When set to "best", any results missing its best reconstruction volume is recomputed
 When unset (default), any results older than a week (or missing) is recomputed
 When set to "all", any previous results is ignored and all results are recomputed
- volumeSize [=256x256x256]
 Size of the reconstruction volume in voxels
- projectionSize [=256x192]
 Size of the projection images in pixels
- trajectory = single,double,adaptive
 Single: Projections are distributed on a single helicoidal path around the sample.
 Double: Projections are distributed between two opposite helicoidal path around the sample.
 Adaptive: Projections are distributed on a single helicoidal path with half rotations at both ends.
- rotationCount [=optimal,4,2,1]
 Total number of revolutions
 "optimal" computes the rotation count so that projections are equally spaced along both height and angle.
 "trajectory=adaptive" adds one revolution to account for the ends.
- photonCount [=8192,4096,2048,0]
 Number of photons per pixel.
 \note 0 means no poisson noise is simulated.
- projectionCount [=128,256,512]
 Total number of projections.
- method = SART,MLTR,CG
 SART: Simultaneous iterative algebraic reconstruction technique
 MLTR: Maximum likelihood expectation maximization for transmission tomography
 CG: Minimizes |Ax-b|² using conjugated gradient (on the normal equations)
- subsetSize
 Number of projections per subsets
 CG does not supports subsets.
 Defaults to √projectionCount for MLTR, 2√projectionCount for SART
